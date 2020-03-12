//# Region.cc: implementation of class for managing a region

#include "Region.h"

#include <atomic>

#include <casacore/casa/Quanta/Quantum.h>
#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>
#include <casacore/measures/Measures/MCDirection.h>

using namespace carta;

Region::Region(int file_id, const std::string& name, CARTA::RegionType type, const std::vector<CARTA::Point>& points, float rotation,
    const casacore::CoordinateSystem& csys)
    : _valid(false), _region_state_changed(false), _region_changed(false) {
    // validate and set region parameters
    _valid = UpdateState(file_id, name, type, points, rotation, csys);
}

// *************************************************************************
// Region settings

bool Region::UpdateState(int file_id, const std::string& name, CARTA::RegionType type, const std::vector<CARTA::Point>& points,
    float rotation, const casacore::CoordinateSystem& csys) {
    // Set region parameters and flags for state change
    bool valid_points = CheckPoints(points, type);
    if (valid_points) {
        RegionState new_state;
        new_state.reference_file_id = file_id;
        new_state.name = name;
        new_state.type = type;
        new_state.control_points = points;
        new_state.rotation = rotation;

        // discern changes
        _region_state_changed = (_region_state != new_state);
        _region_changed = (_region_state.RegionChanged(new_state));

        // set new region state and coord sys
        _region_state = new_state;
        _coord_sys = csys;
    } else { // keep existing state
        _region_state_changed = false;
        _region_changed = false;
    }
    return valid_points;
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
}

// *************************************************************************
// Apply region to image

casacore::WCRegion* Region::GetImageRegion(int file_id, std::shared_ptr<Frame> frame) {
    // Apply (2D) region to image indicated by file_id/frame; returns nullptr if failed
    // Return stored region
    if (_wcregions.count(file_id)) {
        return _wcregions.at(file_id).get()->cloneRegion(); // copy: this ptr will be owned by ImageRegion
    }

    // Create region
    casacore::WCRegion* wcregion(nullptr);

    // Get wcs control points applied to input image
    std::vector<casacore::Quantity> image_wcs_points;
    if (_wcs_control_points.count(file_id)) {
        // use stored wcs points
        image_wcs_points = _wcs_control_points.at(file_id);
    } else {
        // Get wcs control points for reference image
        std::vector<casacore::Quantity> reference_wcs_points;
        if (!GetReferenceWcsPoints(reference_wcs_points)) {
            return wcregion; // nullptr
        }

        // Use reference wcs points if same file id or same direction frame
        if (file_id == _region_state.reference_file_id) {
            image_wcs_points = reference_wcs_points;
        } else {
            // For now, only allow direction coordinate conversion
            if (!frame->CoordinateSystem().hasDirectionCoordinate() || !_coord_sys.hasDirectionCoordinate()) {
                return wcregion; // nullptr
            }

            casacore::MDirection::Types ref_direction_frame = _coord_sys.directionCoordinate().directionType(false);
            casacore::MDirection::Types image_direction_frame = frame->CoordinateSystem().directionCoordinate().directionType(false);
            if (ref_direction_frame == image_direction_frame) {
                // no conversion needed, use reference points and store under file id
                image_wcs_points = reference_wcs_points;
                _wcs_control_points[file_id] = image_wcs_points;
            } else {
                // wcs points conversion needed from ref direction frame to image direction frame; returns image_wcs_points
                if ((_region_state.type == CARTA::RegionType::ELLIPSE) &&
                    !ConvertEllipseWcsPoints(ref_direction_frame, image_direction_frame, reference_wcs_points, image_wcs_points,
                        frame->CoordinateSystem(), file_id)) {
                    return wcregion; // nullptr
                } else if (!ConvertWcsPoints(ref_direction_frame, image_direction_frame, reference_wcs_points, image_wcs_points)) {
                    return wcregion; // nullptr
                }
            }
        }
    }

    wcregion = CreateWcRegion(image_wcs_points);
    _wcregions.at(file_id) = std::shared_ptr<casacore::WCRegion>(wcregion); // store under file_id

    if (wcregion == nullptr) {
        return wcregion;
    } else {
        return wcregion->cloneRegion();
    }
}

bool Region::GetReferenceWcsPoints(std::vector<casacore::Quantity>& ref_points) {
    // Return ref_points in wcs coordinates from stored points or convert pixel to world points
    int ref_id(_region_state.reference_file_id);
    if (_wcs_control_points.count(ref_id)) {
        ref_points = _wcs_control_points.at(ref_id);
        return !ref_points.empty();
    }

    // convert from pixel to wcs
    std::vector<CARTA::Point> pixel_points(_region_state.control_points);
    std::vector<casacore::Quantity> world_points; // one CARTA point is two world points (x, y)

    switch (_region_state.type) {
        case CARTA::RegionType::POINT: { // one point
            if (CartaPointToWorld(pixel_points[0], world_points)) {
                ref_points.push_back(world_points[0]);
                ref_points.push_back(world_points[1]);
            }
            break;
        }
        case CARTA::RegionType::RECTANGLE: { // two points (cx, cy), (width, height)
            if (!RectanglePointsToWorld(pixel_points, ref_points)) {
                ref_points.clear();
            }
            break;
        }
        case CARTA::RegionType::ELLIPSE: { // two points (cx, cy), (bmaj, bmin)
            if (!EllipsePointsToWorld(pixel_points, ref_points)) {
                ref_points.clear();
            }
            break;
        }
        case CARTA::RegionType::POLYGON: { // n points for vertices
            for (auto& point : pixel_points) {
                if (CartaPointToWorld(point, world_points)) {
                    ref_points.push_back(world_points[0]);
                    ref_points.push_back(world_points[1]);
                } else {
                    ref_points.clear();
                    break;
                }
            }
            break;
        }
        default:
            break;
    }

    // store points
    _wcs_control_points[ref_id] = ref_points;

    return !ref_points.empty();
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
    world_point.resize(2);
    world_point[0] = casacore::Quantity(world_values(0), world_units(0));
    world_point[1] = casacore::Quantity(world_values(1), world_units(1));
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
    casacore::Vector<double> x_wcs = world_coords.row(0);
    casacore::Vector<double> y_wcs = world_coords.row(1);
    casacore::Vector<casacore::String> world_units = _coord_sys.worldAxisUnits();
    // reference points: corners (x0, y0, x1, y1, x2, y2, x3, y3) in world coordinates
    wcs_points.resize(num_points * 2);
    for (int i = 0; i < num_points; i += 2) {
        wcs_points[i] = casacore::Quantity(x_wcs(i), world_units(0));
        wcs_points[i + 1] = casacore::Quantity(y_wcs(i), world_units(1));
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
    _ellipse_rotation[_region_state.reference_file_id] = _region_state.rotation;
    float bmaj(pixel_points[1].x()), bmin(pixel_points[1].y());
    if (bmaj > bmin) {
        // carta rotation is from y-axis, ellipse rotation is from x-axis
        _ellipse_rotation[_region_state.reference_file_id] += 90.0;
    } else {
        // bmaj > bmin required, swapping takes care of 90 deg adjustment
        std::swap(bmaj, bmin);
    }
    // Convert pixel length to world length (nPixels, pixelAxis), set wcs_points
    wcs_points.push_back(_coord_sys.toWorldLength(bmaj, 0));
    wcs_points.push_back(_coord_sys.toWorldLength(bmin, 1));
    return true;
}

bool Region::ConvertWcsPoints(casacore::MDirection::Types from_direction_frame, casacore::MDirection::Types to_direction_frame,
    std::vector<casacore::Quantity>& from_points, std::vector<casacore::Quantity>& to_points) {
    // Convert from_points to to_points (wcs coordinates) using direction frames; returns to_points.
    // Points are (x,y) positions only; for point, corners (rectangle), or vertices (polygon)
    // Conversion code from imageanalysis Annotation regions e.g. AnnPolygon
    to_points.clear();
    try {
        for (int i = 0; i < from_points.size(); i += 2) {
            // Convert MDirection to new frame
            casacore::Quantity from_x(from_points[i]), from_y(from_points[i + 1]);
            casacore::MDirection from_direction(from_x, from_y, from_direction_frame);
            casacore::MDirection to_direction = casacore::MDirection::Convert(from_direction, to_direction_frame)();

            // Extract point from converted MDirection
            casacore::Quantum<casacore::Vector<casacore::Double>> converted_quantities = to_direction.getAngle();
            casacore::Quantity to_x(converted_quantities.getValue()(0), converted_quantities.getUnit());
            casacore::Quantity to_y(converted_quantities.getValue()(1), converted_quantities.getUnit());
            to_points.push_back(to_x);
            to_points.push_back(to_y);
        }
    } catch (casacore::AipsError& err) {
        to_points.clear();
        return false;
    }

    return true;
}

bool Region::ConvertEllipseWcsPoints(casacore::MDirection::Types from_direction_frame, casacore::MDirection::Types to_direction_frame,
    std::vector<casacore::Quantity>& from_points, std::vector<casacore::Quantity>& to_points, const casacore::CoordinateSystem& to_csys,
    int to_file_id) {
    // Convert from_points to to_points (wcs coordinates) using direction frames; returns to_points
    // Points are (x,y) position and (bmaj, bmin) angles; rotation is also converted
    // Conversion code from imageanalysis AnnEllipse
    to_points.clear();
    try {
        // Convert MDirection for center point to new frame
        casacore::Quantity from_x(from_points[0]), from_y(from_points[1]);
        casacore::MDirection from_direction(from_x, from_y, from_direction_frame);
        casacore::MDirection to_direction = casacore::MDirection::Convert(from_direction, to_direction_frame)();

        // Extract point from converted MDirection
        casacore::Quantum<casacore::Vector<casacore::Double>> converted_quantities = to_direction.getAngle();
        casacore::Quantity to_x(converted_quantities.getValue()(0), converted_quantities.getUnit());
        casacore::Quantity to_y(converted_quantities.getValue()(1), converted_quantities.getUnit());
        to_points.push_back(to_x);
        to_points.push_back(to_y);

        // Add bmaj, bmin (already converted to angle quantity)
        casacore::Quantity from_bmaj(from_points[2]), from_bmin(from_points[2]);
        to_points.push_back(from_bmaj);
        to_points.push_back(from_bmin);

        // Convert rotation angle
        casacore::Quantity angle;
        to_csys.directionCoordinate().convert(angle, from_direction_frame);
        // add the clockwise angle rather than subtract because the pixel
        // axes are aligned with the "from" (current) world coordinate system rather
        // than the "to" world coordinate system
        float from_rotation(_ellipse_rotation[_region_state.reference_file_id]);
        _ellipse_rotation[to_file_id] = from_rotation + angle.getValue("deg");
    } catch (casacore::AipsError& err) {
        to_points.clear();
        return false;
    }

    return true;
}

casacore::WCRegion* Region::CreateWcRegion(std::vector<casacore::Quantity>& control_points) {
    // Create WCRegion (world coordinate region in an image) according to type using input wcs control points
    // Returns nullptr if region fails
    casacore::WCRegion* region(nullptr);

    return region;
}
