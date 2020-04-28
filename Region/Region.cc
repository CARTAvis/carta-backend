//# Region.cc: implementation of class for managing a region

#include "Region.h"

#include <chrono>
#include <thread>

#include <casacore/casa/Quanta/Quantum.h>
#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>
#include <casacore/images/Regions/WCBox.h>
#include <casacore/images/Regions/WCEllipsoid.h>
#include <casacore/images/Regions/WCPolygon.h>
#include <casacore/measures/Measures/MCDirection.h>

using namespace carta;

Region::Region(int file_id, const std::string& name, CARTA::RegionType type, const std::vector<CARTA::Point>& points, float rotation,
    const casacore::CoordinateSystem& csys)
    : _valid(false), _region_state_changed(false), _region_changed(false), _ref_region_set(false), _z_profile_count(0) {
    // validate and set region parameters
    _valid = UpdateState(file_id, name, type, points, rotation, csys);
}

Region::Region(const RegionState& state, const casacore::CoordinateSystem& csys)
    : _valid(false), _region_state_changed(false), _region_changed(false), _ref_region_set(false), _z_profile_count(0) {
    _valid = UpdateState(state, csys);
}

// *************************************************************************
// Region settings

bool Region::UpdateState(int file_id, const std::string& name, CARTA::RegionType type, const std::vector<CARTA::Point>& points,
    float rotation, const casacore::CoordinateSystem& csys) {
    // Set region state and update
    RegionState new_state;
    new_state.reference_file_id = file_id;
    new_state.name = name;
    new_state.type = type;
    new_state.control_points = points;
    new_state.rotation = rotation;
    return UpdateState(new_state, csys);
}

bool Region::UpdateState(const RegionState& state, const casacore::CoordinateSystem& csys) {
    // Update region from region state
    bool valid = CheckPoints(state.control_points, state.type);

    if (valid) {
        // discern changes
        _region_state_changed = (_region_state != state);
        _region_changed = (_region_state.RegionChanged(state));

        if (_region_changed) {
            _wcs_control_points.clear();
            _ref_region_set = false;
            if (_applied_regions.count(state.reference_file_id)) {
                _applied_regions.at(state.reference_file_id).reset();
            }
        }

        // set new region state and coord sys
        _region_state = state;
        _coord_sys = csys;
    } else { // keep existing state
        _region_state_changed = false;
        _region_changed = false;
    }

    return valid;
}

// *************************************************************************
// Parameter checking

bool Region::CheckPoints(const std::vector<CARTA::Point>& points, CARTA::RegionType type) {
    // check number of points and that values are finite
    bool points_ok(false);
    size_t npoints(points.size());
    switch (type) {
        case CARTA::POINT: { // [(x, y)]
            points_ok = (npoints == 1) && PointsFinite(points);
            break;
        }
        case CARTA::RECTANGLE: { // [(cx,cy), (width,height)], width/height > 0
            points_ok = (npoints == 2) && PointsFinite(points) && (points[1].x() > 0) && (points[1].y() > 0);
            break;
        }
        case CARTA::ELLIPSE: { // [(cx,cy), (bmaj, bmin)]
            points_ok = (npoints == 2) && PointsFinite(points);
            break;
        }
        case CARTA::POLYGON: { // any number of (x, y) greater than 2, which is a line
            points_ok = (npoints > 2) && PointsFinite(points);
            break;
        }
        default:
            break;
    }

    return points_ok;
}

bool Region::PointsFinite(const std::vector<CARTA::Point>& points) {
    // check if NaN or inf points
    bool points_finite(true);
    for (auto& point : points) {
        if (!std::isfinite(point.x()) || !std::isfinite(point.y())) {
            points_finite = false;
            break;
        }
    }
    return points_finite;
}

// Region connection state (disconnected when region closed)

bool Region::IsConnected() {
    return _connected;
}

void Region::DisconnectCalled() { // to interrupt the running jobs in the Region
    _connected = false;
    while (_z_profile_count > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void Region::IncreaseZProfileCount() {
    ++_z_profile_count;
}

void Region::DecreaseZProfileCount() {
    --_z_profile_count;
}

// *************************************************************************
// Apply region to reference image (LCRegion)

casacore::TableRecord Region::GetImageRegionRecord(
    int file_id, const casacore::CoordinateSystem output_csys, const casacore::IPosition output_shape) {
    // Return Record describing Region applied to coord_sys and image_shape
    casacore::TableRecord record;

    casacore::LCRegion* lattice_region = GetImageRegion(file_id, output_csys, output_shape);
    if (lattice_region) {
        record = lattice_region->toRecord("region");
        if (record.isDefined("region")) {
            record = record.asRecord("region");
        }
    } else {
        // Likely region is outside image, convert world control points
        // if coordinate types are same (Direction or Linear Coordinate)
        switch (_region_state.type) {
            case CARTA::RegionType::POINT:
                record = GetPointRecord(output_csys);
                break;
            case CARTA::RegionType::RECTANGLE: // Rectangle is LCPolygon with 4 corners
            case CARTA::RegionType::POLYGON:
                record = GetPolygonRecord(output_csys);
                break;
            case CARTA::RegionType::ELLIPSE:
                record = GetEllipseRecord(output_csys);
                break;
            default:
                break;
        }

        if (!record.empty()) {
            // Complete record with common fields
            record.define("isRegion", true);
            record.define("comment", "");
            record.define("oneRel", false);
            casacore::Vector<casacore::Int> region_shape(2);
            region_shape(0) = output_shape(0);
            region_shape(1) = output_shape(1);
            record.define("shape", region_shape);
        }
    }

    return record;
}

casacore::LCRegion* Region::GetImageRegion(int file_id, const casacore::CoordinateSystem coord_sys, const casacore::IPosition image_shape) {
    // Convert 2D reference region to image region with input coord_sys and shape
    // Sets _applied_regions item and returns LCRegion
    casacore::LCRegion* null_region(nullptr);

    // Check cache
    if (_applied_regions.count(file_id) && _applied_regions.at(file_id)) {
        std::lock_guard<std::mutex> guard(_region_mutex);
        return _applied_regions.at(file_id)->cloneRegion();
    }

    if (!ReferenceRegionValid()) {
        if (_ref_region_set) {
            return null_region;
        } else {
            SetReferenceRegion();
            _ref_region_set = true; // indicates that attempt was made, to avoid repeated attempts
        }
    }

    if (ReferenceRegionValid()) {
        // Create from stored wcregion
        std::shared_ptr<const casacore::WCRegion> current_region = std::atomic_load(&_ref_region);
        try {
            std::lock_guard<std::mutex> guard(_region_mutex);
            auto lc_region = std::shared_ptr<casacore::LCRegion>(current_region->toLCRegion(coord_sys, image_shape));
            _applied_regions[file_id] = std::move(lc_region);
            return _applied_regions.at(file_id)->cloneRegion(); // copy: this ptr will be owned by ImageRegion
        } catch (const casacore::AipsError& err) {
            // Likely region is outside image
            std::cerr << "Error applying region to file " << file_id << " (toLCRegion): " << err.getMesg() << std::endl;
        }
    }

    return null_region;
}

// *************************************************************************
// Apply region to reference image (WCRegion)

bool Region::ReferenceRegionValid() {
    return _ref_region_set && bool(_ref_region);
}

void Region::SetReferenceRegion() {
    // Create WCRegion (world coordinate region) in the reference image according to type using wcs control points
    // Sets _ref_region (maybe to nullptr)
    casacore::WCRegion* region(nullptr);
    std::vector<CARTA::Point> pixel_points(_region_state.control_points);
    std::vector<casacore::Quantity> world_points; // point holder; one CARTA point is two world points (x, y)
    casacore::IPosition pixel_axes(2, 0, 1);
    casacore::Vector<casacore::Int> abs_rel;
    auto type(_region_state.type);
    try {
        switch (type) {
            case CARTA::POINT: { // [(x, y)] single point
                if (CartaPointToWorld(pixel_points[0], _wcs_control_points)) {
                    // WCBox blc and trc are same point
                    std::lock_guard<std::mutex> guard(_region_mutex);
                    region = new casacore::WCBox(_wcs_control_points, _wcs_control_points, pixel_axes, _coord_sys, abs_rel);
                }
                break;
            }
            case CARTA::RECTANGLE: // [(x, y)] for 4 corners
            case CARTA::POLYGON: { // [(x, y)] for vertices
                if (type == CARTA::RECTANGLE) {
                    if (!RectanglePointsToWorld(pixel_points, _wcs_control_points)) {
                        _wcs_control_points.clear();
                    }
                } else {
                    for (auto& point : pixel_points) {
                        if (CartaPointToWorld(point, world_points)) {
                            _wcs_control_points.push_back(world_points[0]);
                            _wcs_control_points.push_back(world_points[1]);
                        } else {
                            _wcs_control_points.clear();
                            break;
                        }
                    }
                }

                if (!_wcs_control_points.empty()) {
                    // separate x and y in control points, convert from Vector<Quantum> to Quantum<Vector>
                    std::vector<double> x, y;
                    for (size_t i = 0; i < _wcs_control_points.size(); i += 2) {
                        x.push_back(_wcs_control_points[i].getValue());
                        y.push_back(_wcs_control_points[i + 1].getValue());
                    }
                    casacore::Vector<casacore::Double> vx(x), vy(y);
                    casacore::Quantum<casacore::Vector<casacore::Double>> qx, qy;

                    casacore::Vector<casacore::String> world_units = _coord_sys.worldAxisUnits();
                    int coord, world_x_axis, world_y_axis;
                    _coord_sys.findWorldAxis(coord, world_x_axis, 0); // for pixel axis 0
                    _coord_sys.findWorldAxis(coord, world_y_axis, 1); // for pixel axis 1

                    qx = vx;                               // set values
                    qx.setUnit(world_units(world_x_axis)); // set unit
                    qy = vy;                               // set values
                    qy.setUnit(world_units(world_y_axis)); // set unit

                    std::lock_guard<std::mutex> guard(_region_mutex);
                    region = new casacore::WCPolygon(qx, qy, pixel_axes, _coord_sys);
                }
                break;
            }
            case CARTA::ELLIPSE: { // [(cx,cy), (bmaj, bmin)]
                if (EllipsePointsToWorld(pixel_points, _wcs_control_points)) {
                    // control points are in order: xcenter, ycenter, major axis, minor axis
                    casacore::Quantity theta(_ellipse_rotation, "deg");
                    theta.convert("rad");

                    std::lock_guard<std::mutex> guard(_region_mutex);
                    region = new casacore::WCEllipsoid(_wcs_control_points[0], _wcs_control_points[1], _wcs_control_points[2],
                        _wcs_control_points[3], theta, 0, 1, _coord_sys);
                }
                break;
            }
            default:
                break;
        }
    } catch (casacore::AipsError& err) { // region failed
        std::cerr << "ERROR: region type " << type << " failed: " << err.getMesg() << std::endl;
    }

    std::shared_ptr<casacore::WCRegion> shared_region = std::shared_ptr<casacore::WCRegion>(region);
    std::atomic_store(&_ref_region, shared_region);
}

bool Region::CartaPointToWorld(const CARTA::Point& point, std::vector<casacore::Quantity>& world_point) {
    // Converts a CARTA point(x, y) in pixel coordinates to a Quantity vector [x, y] in world coordinates
    // Returns whether conversion was successful

    // Vectors must be same number of axes as in coord system for conversion:
    int naxes(_coord_sys.nPixelAxes());
    casacore::Vector<casacore::Double> pixel_values(naxes), world_values(naxes);
    pixel_values = 0.0; // set "extra" axes to 0, not needed
    pixel_values(0) = point.x();
    pixel_values(1) = point.y();

    // convert pixel vector to world vector
    if (!_coord_sys.toWorld(world_values, pixel_values)) {
        return false;
    }

    // Set Quantities from world values and units
    casacore::Vector<casacore::String> world_units = _coord_sys.worldAxisUnits();
    int coord, world_x_axis, world_y_axis;
    _coord_sys.findWorldAxis(coord, world_x_axis, 0); // for pixel axis 0
    _coord_sys.findWorldAxis(coord, world_y_axis, 1); // for pixel axis 1

    world_point.resize(2);
    world_point[0] = casacore::Quantity(world_values(0), world_units(world_x_axis));
    world_point[1] = casacore::Quantity(world_values(1), world_units(world_y_axis));
    return true;
}

bool Region::RectanglePointsToWorld(std::vector<CARTA::Point>& pixel_points, std::vector<casacore::Quantity>& wcs_points) {
    // Convert CARTA rectangle points (cx, cy), (width, height) to world coordinates
    if (pixel_points.size() != 2) {
        return false;
    }

    // Get 4 corner points in pixel coordinates
    int num_points(4);
    float center_x(pixel_points[0].x()), center_y(pixel_points[0].y());
    float width(pixel_points[1].x()), height(pixel_points[1].y());
    casacore::Vector<casacore::Double> x(num_points), y(num_points);
    float rotation(_region_state.rotation);

    if (rotation == 0.0) {
        float x_min(center_x - width / 2.0f), x_max(center_x + width / 2.0f);
        float y_min(center_y - height / 2.0f), y_max(center_y + height / 2.0f);
        // Bottom left
        x(0) = x_min;
        y(0) = y_min;
        // Bottom right
        x(1) = x_max;
        y(1) = y_min;
        // Top right
        x(2) = x_max;
        y(2) = y_max;
        // Top left
        x(3) = x_min;
        y(3) = y_max;
    } else {
        // Apply rotation matrix to get width and height vectors in rotated basis
        float cos_x = cos(rotation * M_PI / 180.0f);
        float sin_x = sin(rotation * M_PI / 180.0f);
        float width_vector_x = cos_x * width;
        float width_vector_y = sin_x * width;
        float height_vector_x = -sin_x * height;
        float height_vector_y = cos_x * height;

        // Bottom left
        x(0) = center_x + (-width_vector_x - height_vector_x) / 2.0f;
        y(0) = center_y + (-width_vector_y - height_vector_y) / 2.0f;
        // Bottom right
        x(1) = center_x + (width_vector_x - height_vector_x) / 2.0f;
        y(1) = center_y + (width_vector_y - height_vector_y) / 2.0f;
        // Top right
        x(2) = center_x + (width_vector_x + height_vector_x) / 2.0f;
        y(2) = center_y + (width_vector_y + height_vector_y) / 2.0f;
        // Top left
        x(3) = center_x + (-width_vector_x + height_vector_x) / 2.0f;
        y(3) = center_y + (-width_vector_y + height_vector_y) / 2.0f;
    }

    // Convert corners to wcs in one call for efficiency
    int num_axes(_coord_sys.nPixelAxes());
    casacore::Matrix<casacore::Double> pixel_coords(num_axes, num_points);
    casacore::Matrix<casacore::Double> world_coords(num_axes, num_points);
    pixel_coords = 0.0;
    pixel_coords.row(0) = x;
    pixel_coords.row(1) = y;
    casacore::Vector<casacore::Bool> failures;
    if (!_coord_sys.toWorldMany(world_coords, pixel_coords, failures)) {
        return false;
    }

    // Save x and y values, units as quantities
    casacore::Vector<casacore::String> world_units = _coord_sys.worldAxisUnits();
    int coord, world_x_axis, world_y_axis;
    _coord_sys.findWorldAxis(coord, world_x_axis, 0); // for pixel axis 0
    _coord_sys.findWorldAxis(coord, world_y_axis, 1); // for pixel axis 1

    casacore::Vector<double> x_wcs = world_coords.row(0);
    casacore::Vector<double> y_wcs = world_coords.row(1);
    // reference points: corners (x0, y0, x1, y1, x2, y2, x3, y3) in world coordinates
    wcs_points.resize(num_points * 2);
    for (int i = 0; i < num_points; ++i) {
        wcs_points[i * 2] = casacore::Quantity(x_wcs(i), world_units(world_x_axis));
        wcs_points[(i * 2) + 1] = casacore::Quantity(y_wcs(i), world_units(world_y_axis));
    }
    return true;
}

bool Region::EllipsePointsToWorld(std::vector<CARTA::Point>& pixel_points, std::vector<casacore::Quantity>& wcs_points) {
    // Convert CARTA ellipse points (cx, cy), (bmaj, bmin) to world coordinates
    if (pixel_points.size() != 2) {
        return false;
    }

    std::vector<casacore::Quantity> center_world;
    if (!CartaPointToWorld(pixel_points[0], center_world)) {
        return false;
    }

    // Store center point quantities
    wcs_points = center_world;

    // Convert bmaj, bmin from pixel length to world length; bmaj > bmin for WCEllipsoid and adjust rotation angle
    _ellipse_rotation = _region_state.rotation;
    float bmaj(pixel_points[1].x()), bmin(pixel_points[1].y());
    if (bmaj > bmin) {
        // carta rotation is from y-axis, ellipse rotation is from x-axis
        _ellipse_rotation += 90.0;
    } else {
        // bmaj > bmin required, swapping takes care of 90 deg adjustment
        std::swap(bmaj, bmin);
    }
    // Convert pixel length to world length (nPixels, pixelAxis), set wcs_points
    wcs_points.push_back(_coord_sys.toWorldLength(bmaj, 0));
    wcs_points.push_back(_coord_sys.toWorldLength(bmin, 1));
    return true;
}

casacore::TableRecord Region::GetPointRecord(const casacore::CoordinateSystem& output_csys) {
    // Return point applied to output_csys in format of LCBox::toRecord()
    casacore::TableRecord record;
    try {
        casacore::Vector<casacore::Double> pixel_point;
        if (_coord_sys.hasDirectionCoordinate() && output_csys.hasDirectionCoordinate()) {
            // Input and output direction reference frames
            casacore::MDirection::Types reference_dir_type = _coord_sys.directionCoordinate().directionType();
            casacore::MDirection::Types output_dir_type = output_csys.directionCoordinate().directionType();

            // Convert reference world coord point to output coord sys
            casacore::MDirection world_direction(_wcs_control_points[0], _wcs_control_points[1], reference_dir_type);
            if (reference_dir_type != output_dir_type) {
                world_direction = casacore::MDirection::Convert(world_direction, output_dir_type)();
            }

            // Convert output world point to output pixel point
            output_csys.directionCoordinate().toPixel(pixel_point, world_direction);
        } else if (_coord_sys.hasLinearCoordinate() && output_csys.hasLinearCoordinate()) {
            // Convert reference world coord point to output coord sys units
            casacore::Vector<casacore::String> world_units = output_csys.worldAxisUnits();
            int coord, world_x_axis, world_y_axis;
            output_csys.findWorldAxis(coord, world_x_axis, 0); // for pixel axis 0
            output_csys.findWorldAxis(coord, world_y_axis, 1); // for pixel axis 1

            casacore::Vector<casacore::Double> world_point(2);
            world_point(0) = _wcs_control_points[0].get(world_units(world_x_axis)).getValue();
            world_point(1) = _wcs_control_points[1].get(world_units(world_y_axis)).getValue();

            // Convert world point to output pixel point
            casacore::Vector<casacore::Double> pixel_point;
            output_csys.toPixel(pixel_point, world_point);
        }

        // Complete record
        casacore::Vector<casacore::Float> blc(2);
        blc(0) = pixel_point(0);
        blc(1) = pixel_point(1);

        record.define("name", "LCBox");
        record.define("blc", blc);
        record.define("trc", blc);
        return record;
    } catch (const casacore::AipsError& err) {
        std::cerr << "Error converting point to image: " << err.getMesg() << std::endl;
        return record;
    }
}

casacore::TableRecord Region::GetPolygonRecord(const casacore::CoordinateSystem& output_csys) {
    // Return region applied to output_csys in format of LCPolygon::toRecord()
    casacore::TableRecord record;
    try {
        size_t npoints(_wcs_control_points.size() / 2);
        casacore::Vector<casacore::Float> x(npoints), y(npoints);
        if (_coord_sys.hasDirectionCoordinate() && output_csys.hasDirectionCoordinate()) {
            // Input and output direction reference frames
            casacore::MDirection::Types reference_dir_type = _coord_sys.directionCoordinate().directionType();
            casacore::MDirection::Types output_dir_type = output_csys.directionCoordinate().directionType();

            for (size_t i = 0; i < _wcs_control_points.size(); i += 2) {
                // Convert reference world coord point to output coord sys
                casacore::MDirection world_direction(_wcs_control_points[0], _wcs_control_points[1], reference_dir_type);
                if (reference_dir_type != output_dir_type) {
                    world_direction = casacore::MDirection::Convert(world_direction, output_dir_type)();
                }

                // Convert output world point to output pixel point
                casacore::Vector<casacore::Double> pixel_point;
                output_csys.directionCoordinate().toPixel(pixel_point, world_direction);

                // Add to x and y Vectors
                int index(i / 2);
                x(index) = pixel_point(0);
                y(index) = pixel_point(1);
            }
        } else if (_coord_sys.hasLinearCoordinate() && output_csys.hasLinearCoordinate()) {
            casacore::Vector<casacore::String> world_units = output_csys.worldAxisUnits();
            int coord, world_x_axis, world_y_axis;
            output_csys.findWorldAxis(coord, world_x_axis, 0);
            output_csys.findWorldAxis(coord, world_y_axis, 1);

            for (size_t i = 0; i < _wcs_control_points.size(); i += 2) {
                // Convert reference world coord point to output coord sys units
                casacore::Vector<casacore::Double> world_point(2);
                world_point(0) = _wcs_control_points[i].get(world_units(world_x_axis)).getValue();
                world_point(1) = _wcs_control_points[i + 1].get(world_units(world_y_axis)).getValue();

                // Convert world point to output pixel point
                casacore::Vector<casacore::Double> pixel_point;
                output_csys.toPixel(pixel_point, world_point);

                // Add to x and y Vectors
                int index(i / 2);
                x(index) = pixel_point(0);
                y(index) = pixel_point(1);
            }
        } else {
            return record;
        }

        // Add fields for this region type
        record.define("name", "LCPolygon");
        record.define("x", x);
        record.define("y", y);

        // Add rotation for rectangle
        if ((_region_state.type == CARTA::RegionType::RECTANGLE) && (_region_state.rotation != 0.0)) {
            casacore::Quantity theta(_region_state.rotation, "deg");
            theta.convert("rad");
            record.define("theta", theta.getValue());
        }

        return record;
    } catch (const casacore::AipsError& err) {
        std::cerr << "Error converting rectangle/polygon to image: " << err.getMesg() << std::endl;
        return casacore::TableRecord();
    }
}

casacore::TableRecord Region::GetEllipseRecord(const casacore::CoordinateSystem& output_csys) {
    // Return region applied to output_csys in format of LCEllipsoid::toRecord()
    casacore::TableRecord record;
    try {
        // Get output increments and units for radii conversion
        casacore::Vector<casacore::Double> increments(output_csys.increment());
        casacore::Vector<casacore::String> world_units = output_csys.worldAxisUnits();
        int coord, world_x_axis, world_y_axis;
        output_csys.findWorldAxis(coord, world_x_axis, 0); // for pixel axis 0
        output_csys.findWorldAxis(coord, world_y_axis, 1); // for pixel axis 1

        // Values to set in Record
        casacore::Vector<casacore::Float> center(2), radii(2);

        if (_coord_sys.hasDirectionCoordinate() && output_csys.hasDirectionCoordinate()) {
            // Input and output direction reference frames
            casacore::MDirection::Types reference_dir_type = _coord_sys.directionCoordinate().directionType();
            casacore::MDirection::Types output_dir_type = output_csys.directionCoordinate().directionType();

            // Convert center point from reference to output coord sys
            casacore::MDirection world_direction(_wcs_control_points[0], _wcs_control_points[1], reference_dir_type);
            if (reference_dir_type != output_dir_type) {
                world_direction = casacore::MDirection::Convert(world_direction, output_dir_type)();
            }

            // Convert output center world point to pixel point
            casacore::Vector<casacore::Double> pixel_point;
            output_csys.directionCoordinate().toPixel(pixel_point, world_direction);

            // Set center
            center(0) = pixel_point(0);
            center(1) = pixel_point(1);
        } else if (_coord_sys.hasLinearCoordinate() && output_csys.hasLinearCoordinate()) {
            // Convert center point from reference to output coord sys unit
            casacore::Vector<casacore::Double> world_point(2);
            world_point(0) = _wcs_control_points[0].get(world_units(world_x_axis)).getValue();
            world_point(1) = _wcs_control_points[1].get(world_units(world_y_axis)).getValue();

            // Convert world point to output pixel point
            casacore::Vector<casacore::Double> pixel_point;
            output_csys.toPixel(pixel_point, world_point);

            // Set center
            center(0) = pixel_point(0);
            center(1) = pixel_point(1);
        } else {
            return record;
        }

        // Convert radii to output world units, then to pixels
        casacore::Quantity bmaj = _wcs_control_points[2];
        bmaj.convert(world_units(world_x_axis));
        casacore::Quantity bmin = _wcs_control_points[3];
        bmin.convert(world_units(world_y_axis));
        radii(0) = fabs(bmaj.getValue() / increments(world_x_axis));
        radii(1) = fabs(bmin.getValue() / increments(world_y_axis));

        // Add fields for this region type
        record.define("name", "LCEllipsoid");
        record.define("center", center);
        record.define("radii", radii);
        casacore::Quantity theta(_ellipse_rotation, "deg");
        theta.convert("rad");
        record.define("theta", theta.getValue());
        return record;
    } catch (const casacore::AipsError& err) {
        std::cerr << "Error converting ellipse to image: " << err.getMesg() << std::endl;
        return casacore::TableRecord();
    }
}
