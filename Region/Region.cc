//# Region.cc: implementation of class for managing a region

#include "Region.h"

#include <atomic>

#include <casacore/casa/Quanta/Quantum.h>
#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>
#include <casacore/images/Regions/WCBox.h>
#include <casacore/images/Regions/WCEllipsoid.h>
#include <casacore/images/Regions/WCPolygon.h>
#include <casacore/measures/Measures/MCDirection.h>

using namespace carta;

Region::Region(int file_id, const std::string& name, CARTA::RegionType type, const std::vector<CARTA::Point>& points, float rotation,
    const casacore::CoordinateSystem& csys)
    : _valid(false), _region_state_changed(false), _region_changed(false), _wcregion_set(false) {
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

        if (_region_changed) {
            _wcs_control_points.clear();
            _wcregion_set = false;
        }

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
// Apply region to reference image

bool Region::RegionValid() {
    return _wcregion_set && bool(_wcregion);
}

casacore::WCRegion* Region::GetReferenceImageRegion() {
    // Apply (2D) region to reference image

    // Return stored region if possible
    if (RegionValid()) {
        std::shared_ptr<const casacore::WCRegion> current_region = std::atomic_load(&_wcregion);
        std::lock_guard<std::mutex> guard(_region_mutex);
        return current_region->cloneRegion(); // copy: this ptr will be owned by ImageRegion
    }

    // Set region
    if (!_wcregion_set) {
        SetReferenceRegion();
        _wcregion_set = true; // indicates that attempt was made
    }

    if (RegionValid()) {
        std::shared_ptr<const casacore::WCRegion> current_region = std::atomic_load(&_wcregion);
        std::lock_guard<std::mutex> guard(_region_mutex);
        return current_region->cloneRegion(); // copy: this ptr will be owned by ImageRegion
    }
        
    return nullptr;
}

void Region::SetReferenceRegion() {
    // Create WCRegion (world coordinate region in the reference image) according to type using wcs control points
    // Sets _wcregion (maybe to nullptr)
	casacore::WCRegion* region;
    std::vector<CARTA::Point> pixel_points(_region_state.control_points);
    std::vector<casacore::Quantity> world_points;       // point holder; one CARTA point is two world points (x, y)
    casacore::IPosition pixel_axes(2, 0, 1);
    casacore::Vector<casacore::Int> abs_rel;

    auto type(_region_state.type);
    try {
        switch (type) {
            case CARTA::POINT: { // [(x, y)] single point
                if (CartaPointToWorld(pixel_points[0], _wcs_control_points)) {
                    // WCBox blc and trc are same point
                    std::lock_guard<std::mutex> guard(_region_mutex);
                    region = new casacore::WCBox(world_points, world_points, pixel_axes, _coord_sys, abs_rel);
                }
                break;
            }
            case CARTA::RECTANGLE: // [(x,y)] for 4 corners
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
                    qx = vx;                    // set values
                    qx.setUnit(world_units(0)); // set unit
                    qy = vy;                    // set values
                    qy.setUnit(world_units(1)); // set unit

                    std::lock_guard<std::mutex> guard(_region_mutex);
                    region = new casacore::WCPolygon(qx, qy, pixel_axes, _coord_sys);
	    		}
                break;
            }
            case CARTA::ELLIPSE: { // [(cx,cy), (bmaj, bmin)]
                if (EllipsePointsToWorld(pixel_points, _wcs_control_points)) {
                    // control points are in order: xcenter, ycenter, major axis, minor axis
                    casacore::Quantity theta(_ellipse_rotation * (M_PI / 180.0f), "rad");

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
    std::atomic_store(&_wcregion, shared_region);
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
