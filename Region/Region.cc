//# Region.cc: implementation of class for managing a region

#include "Region.h"

#include <chrono>
#include <thread>

#include <casacore/casa/Quanta/Quantum.h>
#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>
#include <casacore/images/Regions/WCBox.h>
#include <casacore/images/Regions/WCEllipsoid.h>
#include <casacore/images/Regions/WCPolygon.h>
#include <casacore/lattices/LRegions/LCBox.h>
#include <casacore/lattices/LRegions/LCEllipsoid.h>
#include <casacore/lattices/LRegions/LCExtension.h>
#include <casacore/lattices/LRegions/LCPolygon.h>
#include <casacore/measures/Measures/MCDirection.h>

#include "../InterfaceConstants.h"

using namespace carta;

Region::Region(const RegionState& state, casacore::CoordinateSystem* csys)
    : _coord_sys(csys), _valid(false), _region_changed(false), _reference_region_set(false), _z_profile_count(0) {
    _valid = UpdateRegion(state);
}

Region::~Region() {
    delete _coord_sys;
}

// *************************************************************************
// Region settings

bool Region::UpdateRegion(const RegionState& state) {
    // Update region from region state
    bool valid = CheckPoints(state.control_points, state.type);

    if (valid) {
        // discern changes
        _region_changed = (_region_state.RegionChanged(state));
        if (_region_changed) {
            ResetRegionCache();
        }

        // set new region state
        _region_state = state;
    } else { // keep existing state
        _region_changed = false;
    }

    return valid;
}

void Region::ResetRegionCache() {
    // Invalid when region changes
    _wcs_control_points.clear();
    _reference_region_set = false;
    std::lock_guard<std::mutex> guard(_region_mutex);
    _reference_region.reset();
    _applied_regions.clear();
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

// ******************************************************************************************
// Apply region to reference image in world coordinates (WCRegion) and save wcs control points

bool Region::ReferenceRegionValid() {
    return _reference_region_set && bool(_reference_region);
}

void Region::SetReferenceRegion() {
    // Create WCRegion (world coordinate region) in the reference image according to type using wcs control points
    // Sets _reference_region (maybe to nullptr)
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
                    region = new casacore::WCBox(_wcs_control_points, _wcs_control_points, pixel_axes, *_coord_sys, abs_rel);
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

                    casacore::Vector<casacore::String> world_units = _coord_sys->worldAxisUnits();

                    qx = vx;                    // set values
                    qx.setUnit(world_units(0)); // set unit
                    qy = vy;                    // set values
                    qy.setUnit(world_units(1)); // set unit

                    std::lock_guard<std::mutex> guard(_region_mutex);
                    region = new casacore::WCPolygon(qx, qy, pixel_axes, *_coord_sys);
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
                        _wcs_control_points[3], theta, 0, 1, *_coord_sys);
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
    std::atomic_store(&_reference_region, shared_region);
}

bool Region::CartaPointToWorld(const CARTA::Point& point, std::vector<casacore::Quantity>& world_point) {
    // Converts a CARTA point(x, y) in pixel coordinates to a Quantity vector [x, y] in world coordinates
    // Returns whether conversion was successful

    // Vectors must be same number of axes as in coord system for conversion:
    int naxes(_coord_sys->nPixelAxes());
    casacore::Vector<casacore::Double> pixel_values(naxes), world_values(naxes);
    pixel_values = 0.0; // set "extra" axes to 0, not needed
    pixel_values(0) = point.x();
    pixel_values(1) = point.y();

    // convert pixel vector to world vector
    if (!_coord_sys->toWorld(world_values, pixel_values)) {
        return false;
    }

    // Set Quantities from world values and units
    casacore::Vector<casacore::String> world_units = _coord_sys->worldAxisUnits();

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
    int num_axes(_coord_sys->nPixelAxes());
    casacore::Matrix<casacore::Double> pixel_coords(num_axes, num_points);
    casacore::Matrix<casacore::Double> world_coords(num_axes, num_points);
    pixel_coords = 0.0;
    pixel_coords.row(0) = x;
    pixel_coords.row(1) = y;
    casacore::Vector<casacore::Bool> failures;
    if (!_coord_sys->toWorldMany(world_coords, pixel_coords, failures)) {
        return false;
    }

    // Save x and y values, units as quantities
    casacore::Vector<casacore::String> world_units = _coord_sys->worldAxisUnits();
    casacore::Vector<double> x_wcs = world_coords.row(0);
    casacore::Vector<double> y_wcs = world_coords.row(1);
    // reference points: corners (x0, y0, x1, y1, x2, y2, x3, y3) in world coordinates
    wcs_points.resize(num_points * 2);
    for (int i = 0; i < num_points; ++i) {
        wcs_points[i * 2] = casacore::Quantity(x_wcs(i), world_units(0));
        wcs_points[(i * 2) + 1] = casacore::Quantity(y_wcs(i), world_units(1));
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
    wcs_points.push_back(_coord_sys->toWorldLength(bmaj, 0));
    wcs_points.push_back(_coord_sys->toWorldLength(bmin, 1));
    return true;
}

// *************************************************************************
// Apply region to any image

casacore::LCPolygon* Region::GetImageRegion(int file_id, const casacore::CoordinateSystem& output_csys, const casacore::IPosition& output_shape) {
    // Apply region to any image as converted polygon vertices

    // Check polygon regions cache
    casacore::LCPolygon* lc_polygon = GetCachedPolygonRegion(file_id);

    if (!lc_polygon) {
        // Apply reference polygon vertices to output image
        lc_polygon = GetAppliedPolygonRegion(file_id, output_csys, output_shape);

        if (lc_polygon) {
            // Cache converted polygon
            std::lock_guard<std::mutex> guard(_region_mutex);
            casacore::LCPolygon* poly_copy = static_cast<casacore::LCPolygon*>(lc_polygon->cloneRegion());
            auto polygon_region = std::shared_ptr<casacore::LCPolygon>(poly_copy);
            _polygon_regions[file_id] = std::move(polygon_region);
        }
    }

    return lc_polygon;
}

casacore::LCPolygon* Region::GetCachedPolygonRegion(int file_id) {
    // Return cached polygon region applied to image with file_id
    casacore::LCPolygon* lc_polygon(nullptr);
    if (_polygon_regions.count(file_id)) {
        std::unique_lock<std::mutex> ulock(_region_mutex);
        lc_polygon = static_cast<casacore::LCPolygon*>(_polygon_regions.at(file_id)->cloneRegion());
        ulock.unlock();
    }
    return lc_polygon;
}

casacore::LCPolygon* Region:: GetAppliedPolygonRegion(
    int file_id, const casacore::CoordinateSystem& output_csys, const casacore::IPosition& output_shape) {
    // Approximate region as polygon pixels, and convert to given csys
    casacore::LCPolygon* lc_polygon(nullptr);

    if (_polygon_control_points.empty() && !SetRegionPolygonPoints(DEFAULT_VERTEX_COUNT)) {
        return lc_polygon;
    }

    try {
        casacore::Vector<casacore::Double> x(DEFAULT_VERTEX_COUNT), y(DEFAULT_VERTEX_COUNT);
        bool convert_ok(true);

        for (auto i = 0; i < DEFAULT_VERTEX_COUNT; ++i) {
            // Convert polygon pixel point to world
            std::vector<casacore::Quantity> world_point;
            if (CartaPointToWorld(_polygon_control_points[i], world_point)) {
                // Convert polygon world point to output csys
                casacore::Vector<casacore::Double> pixel_point;
                if (ConvertedWorldToPixel(world_point, output_csys, pixel_point)) {
                    x[i] = pixel_point[0];
                    y[i] = pixel_point[1];
                } else { // polygon reference world to output pixel failed
                    convert_ok = false;
                    break;
                }
            } else { // polygon reference pixel to world failed
                convert_ok = false;
                break;
            }
        }

        if (convert_ok) {
            lc_polygon = new casacore::LCPolygon(x, y, output_shape);
        } else {
            std::cerr << "Error approximating region as polygon in matched image " << std::endl;
        }
    } catch (const casacore::AipsError& err) {
        std::cerr << "Error approximating region as polygon: " << err.getMesg() << std::endl;
    }

    return lc_polygon;        
}

bool Region::SetRegionPolygonPoints(int npoints) {
    // Approximates region as polygon with npoints vertices.
    // Sets _polygon_control_points in reference image pixel coordinates
    // TODO !!!
    return false;
}

casacore::ArrayLattice<casacore::Bool> Region::GetImageRegionMask(int file_id) {
    // Return pixel mask for this region; requires that image region for this file id has been set.
    // Otherwise mask is empty array.
    casacore::ArrayLattice<casacore::Bool> mask;
    if (_applied_regions.count(file_id) && _applied_regions.at(file_id)) {
        std::unique_lock<std::mutex> ulock(_region_mutex);
        auto lcregion = _applied_regions.at(file_id)->cloneRegion();
        auto extended_region = static_cast<casacore::LCExtension*>(lcregion);
        if (extended_region) {
            switch (_region_state.type) {
                case CARTA::POINT: {
                    auto region = static_cast<const casacore::LCBox&>(extended_region->region());
                    mask = region.getMask();
                    break;
                }
                case CARTA::RECTANGLE:
                case CARTA::POLYGON: {
                    auto region = static_cast<const casacore::LCPolygon&>(extended_region->region());
                    mask = region.getMask();
                    break;
                }
                case CARTA::ELLIPSE: {
                    auto region = static_cast<const casacore::LCEllipsoid&>(extended_region->region());
                    mask = region.getMask();
                    break;
                }
                default:
                    break;
            }
        }
        ulock.unlock();
    }

    return mask;
}

// ***************************************************************
// Apply region to any image and return LCRegion Record for export

casacore::TableRecord Region::GetImageRegionRecord(
    int file_id, const casacore::CoordinateSystem& output_csys, const casacore::IPosition& output_shape) {
    // Return Record describing Region applied to output coord sys and image_shape in pixel coordinates
    casacore::TableRecord record;

    // Get converted LCRegion
    // Check applied regions cache
    casacore::LCRegion* lc_region = GetCachedLCRegion(file_id);

    if (!lc_region) {
        // Apply reference region to output image
        lc_region = GetConvertedLCRegion(file_id, output_csys, output_shape);
    }

    if (lc_region) {
        // Convert LCRegion to Record
        record = lc_region->toRecord("region");
        if (record.isDefined("region")) {
            record = record.asRecord("region");
        }
    }

    if (record.empty()) {
        // LCRegion failed, is outside the image or a rotated rectangle.
        // Manually convert control points and put in Record.
        record = GetRegionPointsRecord(file_id, output_csys, output_shape);
    }

    return record;
}

casacore::LCRegion* Region::GetCachedLCRegion(int file_id) {
    // Return cached region applied to image with file_id
    casacore::LCRegion* lc_region(nullptr);

    if (_applied_regions.count(file_id)) {
        std::unique_lock<std::mutex> ulock(_region_mutex);
        lc_region = _applied_regions.at(file_id)->cloneRegion();
        ulock.unlock();
    }

    return lc_region;
}

casacore::LCRegion* Region::GetConvertedLCRegion(
    int file_id, const casacore::CoordinateSystem& output_csys, const casacore::IPosition& output_shape) {
    // Convert 2D reference WCRegion to LCRegion with input coord_sys and shape
    casacore::LCRegion* lc_region(nullptr);

    if (IsRotbox()) {
        // Cannot convert region, it is a polygon type.
        return lc_region;
    }

    // Convert reference WCRegion to LCRegion using given csys and shape
    try {
        if (!_reference_region_set) {
            SetReferenceRegion();
            // Flag indicates that attempt was made, to avoid repeated attempts
            _reference_region_set = true;
        }

        std::lock_guard<std::mutex> guard(_region_mutex);
        if (ReferenceRegionValid()) {
            std::shared_ptr<const casacore::WCRegion> reference_region = std::atomic_load(&_reference_region);
            lc_region = reference_region->toLCRegion(output_csys, output_shape);
        }
    } catch (const casacore::AipsError& err) {
        std::cerr << "Error converting region to file " << file_id << ": " << err.getMesg() << std::endl;
    }

    if (lc_region) {
        // Make a copy and cache LCRegion in map
        std::lock_guard<std::mutex> guard(_region_mutex);
        casacore::LCRegion* region_copy = lc_region->cloneRegion();
        auto applied_region = std::shared_ptr<casacore::LCRegion>(region_copy);
        _applied_regions[file_id] = std::move(applied_region);
    }

    return lc_region;
}

casacore::TableRecord Region::GetRegionPointsRecord(
    int file_id, const casacore::CoordinateSystem& output_csys, const casacore::IPosition& output_shape) {
    // Convert control points to output coord sys if needed, and return completed record.
    casacore::TableRecord record;
    if (file_id == _region_state.reference_file_id) {
        record = GetControlPointsRecord(output_shape.size());
    } else {
        switch (_region_state.type) {
            case CARTA::RegionType::POINT:
                record = GetPointRecord(output_csys, output_shape);
                break;
            case CARTA::RegionType::RECTANGLE:
            case CARTA::RegionType::POLYGON: {
                // Rectangle is LCPolygon with 4 corners;
                // Rotbox requires special handling to get (unrotated) rectangle corners instead of rotated polygon vertices
                record = (IsRotbox() ? GetRotboxRecord(output_csys) : GetPolygonRecord(output_csys));
                break;
            }
            case CARTA::RegionType::ELLIPSE:
                record = GetEllipseRecord(output_csys);
                break;
            default:
                break;
        }
    }

    if (!record.empty()) {
        // Complete Record with common fields
        record.define("isRegion", 1);
        record.define("comment", "");
        record.define("oneRel", false);
        casacore::Vector<casacore::Int> region_shape;
        if (_region_state.type == CARTA::RegionType::POINT) {
            region_shape = output_shape.asVector(); // LCBox uses entire image shape
        } else {
            region_shape.resize(2);
            region_shape(0) = output_shape(0);
            region_shape(1) = output_shape(1);
        }
        record.define("shape", region_shape);
    }

    return record;
}

casacore::TableRecord Region::GetControlPointsRecord(int ndim) {
    // Return region Record in pixel coords in format of LCRegion::toRecord(); no conversion (for reference image)
    casacore::TableRecord record;

    switch (_region_state.type) {
        case CARTA::RegionType::POINT: {
            casacore::Vector<casacore::Float> blc(ndim, 0.0), trc(ndim, 0.0);
            blc(0) = _region_state.control_points[0].x();
            blc(1) = _region_state.control_points[0].y();
            trc(0) = _region_state.control_points[0].x();
            trc(1) = _region_state.control_points[0].y();

            record.define("name", "LCBox");
            record.define("blc", blc);
            record.define("trc", trc);
            break;
        }
        case CARTA::RegionType::RECTANGLE: {
            // Rectangle is LCPolygon with 4 corners; calculate from center and width/height
            casacore::Vector<casacore::Float> x(4), y(4);
            float center_x = _region_state.control_points[0].x();
            float center_y = _region_state.control_points[0].y();
            float width = _region_state.control_points[1].x();
            float height = _region_state.control_points[1].y();
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

            record.define("name", "LCPolygon");
            record.define("x", x);
            record.define("y", y);
            break;
        }
        case CARTA::RegionType::POLYGON: {
            size_t npoints(_region_state.control_points.size());
            casacore::Vector<casacore::Float> x(npoints + 1), y(npoints + 1);
            for (size_t i = 0; i < npoints; ++i) {
                x(i) = _region_state.control_points[i].x();
                y(i) = _region_state.control_points[i].y();
            }
            // LCPolygon::toRecord includes first point as last point to close region
            x(npoints) = _region_state.control_points[0].x();
            y(npoints) = _region_state.control_points[0].y();

            record.define("name", "LCPolygon");
            record.define("x", x);
            record.define("y", y);
            break;
        }
        case CARTA::RegionType::ELLIPSE: {
            casacore::Vector<casacore::Float> center(2), radii(2);
            center(0) = _region_state.control_points[0].x();
            center(1) = _region_state.control_points[0].y();
            radii(0) = _region_state.control_points[1].x();
            radii(1) = _region_state.control_points[1].y();

            record.define("name", "LCEllipsoid");
            record.define("center", center);
            record.define("radii", radii);

            // LCEllipsoid measured from major (x) axis
            casacore::Quantity theta = casacore::Quantity(_region_state.rotation + 90.0, "deg");
            theta.convert("rad");
            record.define("theta", theta.getValue());
            break;
        }
        default:
            break;
    }

    return record;
}

casacore::TableRecord Region::GetPointRecord(const casacore::CoordinateSystem& output_csys, const casacore::IPosition& output_shape) {
    // Return point applied to output_csys in format of LCBox::toRecord()
    casacore::TableRecord record;
    try {
        // wcs control points is single point in vector
        casacore::Vector<casacore::Double> pixel_point;
        if (ConvertedWorldToPixel(_wcs_control_points, output_csys, pixel_point)) {
            // Complete record
            casacore::Vector<casacore::Float> blc(output_shape.size(), 1.0), trc(output_shape);
            blc(0) = pixel_point(0);
            blc(1) = pixel_point(1);
            trc(0) = pixel_point(0);
            trc(1) = pixel_point(1);

            record.define("name", "LCBox");
            record.define("blc", blc);
            record.define("trc", trc);
        } else {
            std::cerr << "Error converting point to image." << std::endl;
        }
    } catch (const casacore::AipsError& err) {
        std::cerr << "Error converting point to image: " << err.getMesg() << std::endl;
    }

    return record;
}

casacore::TableRecord Region::GetPolygonRecord(const casacore::CoordinateSystem& output_csys) {
    // Return region applied to output_csys in format of LCPolygon::toRecord()
    // This is for POLYGON or RECTANGLE (points are four corners of box)
    casacore::TableRecord record;
    try {
        size_t npoints(_wcs_control_points.size() / 2);
        // Record fields
        casacore::Vector<casacore::Float> x(npoints), y(npoints);

        // Convert each wcs control point to pixel coords in output csys
        for (size_t i = 0; i < _wcs_control_points.size(); i += 2) {
            std::vector<casacore::Quantity> world_point(2);
            world_point[0] = _wcs_control_points[i];
            world_point[1] = _wcs_control_points[i + 1];
            casacore::Vector<casacore::Double> pixel_point;
            if (ConvertedWorldToPixel(world_point, output_csys, pixel_point)) {
                // Add to x and y Vectors
                int index(i / 2);
                x(index) = pixel_point(0);
                y(index) = pixel_point(1);
            } else {
                std::cerr << "Error converting rectangle/polygon to image." << std::endl;
                return record;
            }
        }

        if (_region_state.type == CARTA::RegionType::POLYGON) {
            // LCPolygon::toRecord adds first point as last point to close region
            x.resize(npoints + 1, true);
            x(npoints) = x(0);
            y.resize(npoints + 1, true);
            y(npoints) = y(0);
        }

        // Add fields for this region type
        record.define("name", "LCPolygon");
        record.define("x", x);
        record.define("y", y);
    } catch (const casacore::AipsError& err) {
        std::cerr << "Error converting rectangle/polygon to image: " << err.getMesg() << std::endl;
    }

    return record;
}

casacore::TableRecord Region::GetRotboxRecord(const casacore::CoordinateSystem& output_csys) {
    // Determine corners of unrotated box (control points) applied to output_csys.
    // Return region applied to output_csys in format of LCPolygon::toRecord()
    casacore::TableRecord record;
    try {
        // Get 4 corner points in pixel coordinates
        std::vector<CARTA::Point> pixel_points(_region_state.control_points);
        float center_x(pixel_points[0].x()), center_y(pixel_points[0].y());
        float width(pixel_points[1].x()), height(pixel_points[1].y());

        int num_points(4);
        casacore::Vector<casacore::Double> x(num_points), y(num_points);
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

        // Convert corners to reference world coords
        int num_axes(_coord_sys->nPixelAxes());
        casacore::Matrix<casacore::Double> pixel_coords(num_axes, num_points);
        casacore::Matrix<casacore::Double> world_coords(num_axes, num_points);
        pixel_coords = 0.0;
        pixel_coords.row(0) = x;
        pixel_coords.row(1) = y;
        casacore::Vector<casacore::Bool> failures;
        if (!_coord_sys->toWorldMany(world_coords, pixel_coords, failures)) {
            std::cerr << "Error converting rectangle pixel coordinates to world." << std::endl;
            return record;
        }
        casacore::Vector<casacore::Double> x_wcs = world_coords.row(0);
        casacore::Vector<casacore::Double> y_wcs = world_coords.row(1);

        // Units for Quantities
        casacore::Vector<casacore::String> world_units = output_csys.worldAxisUnits();
        casacore::Vector<casacore::Float> corner_x(num_points), corner_y(num_points);
        for (size_t i = 0; i < num_points; i++) {
            // Convert x and y reference world coords to Quantity
            std::vector<casacore::Quantity> world_point;
            world_point.push_back(casacore::Quantity(x_wcs(i), world_units(0)));
            world_point.push_back(casacore::Quantity(y_wcs(i), world_units(1)));

            // Convert reference world point to output pixel point
            casacore::Vector<casacore::Double> pixel_point;
            if (ConvertedWorldToPixel(world_point, output_csys, pixel_point)) {
                // Add to x and y Vectors
                corner_x(i) = pixel_point(0);
                corner_y(i) = pixel_point(1);
            } else {
                std::cerr << "Error converting rectangle coordinates to image." << std::endl;
                return record;
            }
        }

        // Add fields for this region type
        record.define("name", "LCPolygon");
        record.define("x", corner_x);
        record.define("y", corner_y);
    } catch (const casacore::AipsError& err) {
        std::cerr << "Error converting rectangle to image: " << err.getMesg() << std::endl;
    }

    return record;
}

casacore::TableRecord Region::GetEllipseRecord(const casacore::CoordinateSystem& output_csys) {
    // Return region applied to output_csys in format of LCEllipsoid::toRecord()
    casacore::TableRecord record;

    // Values to set in Record
    casacore::Vector<casacore::Float> center(2), radii(2);

    // Center point
    std::vector<casacore::Quantity> world_point(2);
    world_point[0] = _wcs_control_points[0];
    world_point[1] = _wcs_control_points[1];
    casacore::Vector<casacore::Double> pixel_point;
    try {
        if (ConvertedWorldToPixel(world_point, output_csys, pixel_point)) {
            center(0) = pixel_point(0);
            center(1) = pixel_point(1);

            // Convert radii to output world units, then to pixels
            casacore::Vector<casacore::Double> increments(output_csys.increment());
            casacore::Vector<casacore::String> world_units = output_csys.worldAxisUnits();
            casacore::Quantity bmaj = _wcs_control_points[2];
            bmaj.convert(world_units(0));
            casacore::Quantity bmin = _wcs_control_points[3];
            bmin.convert(world_units(1));
            radii(0) = fabs(bmaj.getValue() / increments(0));
            radii(1) = fabs(bmin.getValue() / increments(1));

            // Add fields for this region type
            record.define("name", "LCEllipsoid");
            record.define("center", center);
            record.define("radii", radii);

            // LCEllipsoid measured from major (x) axis
            // TODO: adjust angle for output csys
            casacore::Quantity theta = casacore::Quantity(_region_state.rotation + 90.0, "deg");
            theta.convert("rad");
            record.define("theta", theta.getValue());
        } else {
            std::cerr << "Incompatible coordinate systems for ellipse conversion." << std::endl;
        }
    } catch (const casacore::AipsError& err) {
        std::cerr << "Error converting ellipse to image: " << err.getMesg() << std::endl;
    }

    return record;
}

bool Region::ConvertedWorldToPixel(std::vector<casacore::Quantity>& world_point, const casacore::CoordinateSystem& output_csys,
    casacore::Vector<casacore::Double>& pixel_point) {
    // Convert input reference world coord to output world coord, then to pixel coord
    // Exception should be caught in calling function for creating error message
    bool success(false);

    if (_coord_sys->hasDirectionCoordinate() && output_csys.hasDirectionCoordinate()) {
        // Input and output direction reference frames
        casacore::MDirection::Types reference_dir_type = _coord_sys->directionCoordinate().directionType();
        casacore::MDirection::Types output_dir_type = output_csys.directionCoordinate().directionType();

        // Convert world point from reference to output coord sys
        casacore::MDirection world_direction(world_point[0], world_point[1], reference_dir_type);
        if (reference_dir_type != output_dir_type) {
            world_direction = casacore::MDirection::Convert(world_direction, output_dir_type)();
        }

        // Convert output world point to pixel point
        output_csys.directionCoordinate().toPixel(pixel_point, world_direction);
        success = true;
    } else if (_coord_sys->hasLinearCoordinate() && output_csys.hasLinearCoordinate()) {
        // Input and output linear frames
        casacore::Vector<casacore::String> output_units = output_csys.worldAxisUnits();
        casacore::Vector<casacore::Double> world_point_value(2);
        world_point_value(0) = world_point[0].get(output_units(0)).getValue();
        world_point_value(1) = world_point[1].get(output_units(1)).getValue();

        // Convert world point to output pixel point
        output_csys.toPixel(pixel_point, world_point_value);
        success = true;
    }

    return success;
}
