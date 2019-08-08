//# Region.cc: implementation of class for managing a region

#include "Region.h"

#include <stdio.h>   // sscanf
#include <algorithm> // max
#include <cmath>     // round

#include <casacore/casa/Arrays/ArrayLogical.h>
#include <casacore/casa/Quanta/Quantum.h>
#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>
#include <casacore/images/Regions/WCEllipsoid.h>
#include <casacore/images/Regions/WCExtension.h>
#include <casacore/images/Regions/WCPolygon.h>
#include <casacore/images/Regions/WCRegion.h>
#include <casacore/lattices/LRegions/LCBox.h>
#include <casacore/lattices/LRegions/LCEllipsoid.h>
#include <casacore/lattices/LRegions/LCExtension.h>
#include <casacore/lattices/LRegions/LCPolygon.h>
#include <casacore/measures/Measures/Stokes.h>

#include <imageanalysis/Annotations/AnnCenterBox.h>
#include <imageanalysis/Annotations/AnnCircle.h>
#include <imageanalysis/Annotations/AnnEllipse.h>
#include <imageanalysis/Annotations/AnnPolygon.h>
#include <imageanalysis/Annotations/AnnRectBox.h>
#include <imageanalysis/Annotations/AnnRegion.h>
#include <imageanalysis/Annotations/AnnRotBox.h>

#include "../InterfaceConstants.h"

using namespace carta;

Region::Region(const std::string& name, const CARTA::RegionType type, const std::vector<CARTA::Point>& points, const float rotation,
    const casacore::IPosition image_shape, int spectral_axis, int stokes_axis, const casacore::CoordinateSystem& coord_sys)
    : _name(name),
      _type(type),
      _rotation(0.0),
      _valid(false),
      _xy_region_changed(false),
      _image_shape(image_shape),
      _spectral_axis(spectral_axis),
      _stokes_axis(stokes_axis),
      _xy_axes(casacore::IPosition(2, 0, 1)),
      _coord_sys(coord_sys) {
    // validate and set region parameters
    _num_dims = image_shape.size();
    _valid = UpdateRegionParameters(name, type, points, rotation);
    if (_valid) {
        _region_stats = std::unique_ptr<RegionStats>(new RegionStats());
        _region_profiler = std::unique_ptr<RegionProfiler>(new RegionProfiler());
    }
}

Region::Region(casacore::CountedPtr<const casa::AnnotationBase> annotation_region, const casacore::IPosition image_shape, int spectral_axis,
    int stokes_axis, const casacore::CoordinateSystem& coord_sys)
    : _rotation(0.0),
      _valid(false),
      _image_shape(image_shape),
      _spectral_axis(spectral_axis),
      _stokes_axis(stokes_axis),
      _xy_axes(casacore::IPosition(2, 0, 1)),
      _xy_region(nullptr),
      _xy_mask(nullptr),
      _coord_sys(coord_sys) {
    // Create region from imported annotation region
    _num_dims = image_shape.size();
    // set name, type, control points, rotation (default 0.0 already set), and xy region
    if (annotation_region) {
        _name = annotation_region->getLabel();
        switch (annotation_region->getType()) {
            case casa::AnnotationBase::RECT_BOX:
            case casa::AnnotationBase::CENTER_BOX: {
                // all rectangles are polygons
                const casa::AnnPolygon* polygon = static_cast<const casa::AnnPolygon*>(annotation_region.get());
                if (polygon != nullptr) {
                    std::atomic_store(&_xy_region, polygon->getRegion2());
                    // get polygon vertices for control points, determine blc and trc
                    std::vector<casacore::Double> x, y;
                    polygon->pixelVertices(x, y);
                    double xmin = *std::min_element(x.begin(), x.end());
                    double xmax = *std::max_element(x.begin(), x.end());
                    double ymin = *std::min_element(y.begin(), y.end());
                    double ymax = *std::max_element(y.begin(), y.end());
                    // set carta rectangle control points
                    if ((xmin == xmax) && (ymin == ymax)) { // point is rectangle with blc=trc
                        _type = CARTA::RegionType::POINT;
                        CARTA::Point point;
                        point.set_x(x[0]);
                        point.set_y(y[0]);
                        _control_points.push_back(point);
                        _valid = true;
                    } else {
                        _type = CARTA::RegionType::RECTANGLE;
                        CARTA::Point point;
                        point.set_x((xmin + xmax) / 2.0); // cx
                        point.set_y((ymin + ymax) / 2.0); // cy
                        _control_points.push_back(point);
                        point.set_x(xmax - xmin); // width
                        point.set_y(ymax - ymin); // height
                        _control_points.push_back(point);
                        _valid = true;
                    }
                }
                break;
            }
            case casa::AnnotationBase::ROTATED_BOX: // rotation angle lost when converted to polygon
            case casa::AnnotationBase::POLYGON: {
                _type = CARTA::RegionType::POLYGON;
                const casa::AnnPolygon* polygon = static_cast<const casa::AnnPolygon*>(annotation_region.get());
                if (polygon != nullptr) {
                    std::atomic_store(&_xy_region, polygon->getRegion2());
                    // get polygon vertices for control points
                    std::vector<casacore::Double> x, y;
                    polygon->pixelVertices(x, y);
                    for (size_t i = 0; i < x.size(); ++i) {
                        CARTA::Point point;
                        point.set_x(x[i]);
                        point.set_y(y[i]);
                        _control_points.push_back(point);
                    }
                    _valid = true;
                }
                break;
            }
            case casa::AnnotationBase::CIRCLE:
            case casa::AnnotationBase::ELLIPSE: {
                _type = CARTA::RegionType::ELLIPSE;
                casa::AnnotationBase::Type ann_type = annotation_region->getType();
                casacore::DirectionCoordinate dir_coord = _coord_sys.directionCoordinate();
                casacore::MDirection center_position;
                casacore::Quantity bmaj, bmin, position_angle;
                bool is_ellipse(true);
                bool have_region_info(false);
                if (ann_type == casa::AnnotationBase::CIRCLE) {
                    const casa::AnnCircle* circle = static_cast<const casa::AnnCircle*>(annotation_region.get());
                    if (circle != nullptr) {
                        std::atomic_store(&_xy_region, circle->getRegion2());
                        center_position = circle->getCenter();
                        bmaj = circle->getRadius();
                        bmin = bmaj;
                        is_ellipse = false;
                        have_region_info = true;
                    }
                } else {
                    // if pixels not square, circle is an AnnEllipse
                    const casa::AnnEllipse* ellipse = static_cast<const casa::AnnEllipse*>(annotation_region.get());
                    if (ellipse != nullptr) {
                        std::atomic_store(&_xy_region, ellipse->getRegion2());
                        center_position = ellipse->getCenter();
                        bmaj = ellipse->getSemiMajorAxis();
                        bmin = ellipse->getSemiMinorAxis();
                        position_angle = ellipse->getPositionAngle();
                        have_region_info = true;
                    }
                }
                if (have_region_info) {
                    // set control point: cx, cy in pixel coords
                    casacore::Vector<casacore::Double> pixel_coords;
                    dir_coord.toPixel(pixel_coords, center_position);
                    CARTA::Point point;
                    point.set_x(pixel_coords[0]);
                    point.set_y(pixel_coords[1]);
                    _control_points.push_back(point);
                    // set control point: bmaj, bmin in npixels
                    double bmaj_pixel = AngleToLength(bmaj, 0);
                    double bmin_pixel = AngleToLength(bmin, 1);
                    point.set_x(bmaj_pixel);
                    point.set_y(bmin_pixel);
                    _control_points.push_back(point);
                    if (is_ellipse) { // set rotation
                        position_angle.convert("deg");
                        _rotation = position_angle.getValue();
                    }
                    _valid = true;
                }
                break;
            }
            case casa::AnnotationBase::POLYLINE:
            case casa::AnnotationBase::ANNULUS:
            default:
                break;
        }
    }
    if (_valid) {
        _region_stats = std::unique_ptr<RegionStats>(new RegionStats());
        _region_profiler = std::unique_ptr<RegionProfiler>(new RegionProfiler());
    }
}

Region::~Region() {}

// *************************************************************************
// Region settings

bool Region::UpdateRegionParameters(
    const std::string name, const CARTA::RegionType type, const std::vector<CARTA::Point>& points, float rotation) {
    // Set region parameters and flag if xy region changed
    bool xy_params_changed((PointsChanged(points) || (rotation != _rotation)));

    _name = name;
    _type = type;
    _rotation = rotation;
    // validate and set points, create LCRegion
    bool points_set(SetPoints(points));
    if (points_set)
        SetXyRegion(points, rotation);

    // region changed if xy params changed and points validated
    _xy_region_changed = xy_params_changed && points_set;
    if (_xy_region_changed && _region_stats)
        _region_stats->ClearStats(); // recalculate everything

    return points_set;
}

bool Region::SetPoints(const std::vector<CARTA::Point>& points) {
    // check and set control points
    bool points_updated(false);
    if (CheckPoints(points)) {
        _control_points = points;
        points_updated = true;
    }
    return points_updated;
}

RegionState Region::GetRegionState() {
    RegionState region_state(_name, _type, _control_points, _rotation);
    return region_state;
}

double Region::AngleToLength(casacore::Quantity angle, unsigned int pixel_axis) {
    // world->pixel conversion of ellipse radius.
    // The opposite of casacore::CoordinateSystem::toWorldLength for pixel->world conversion.
    int coord, coord_axis;
    _coord_sys.findWorldAxis(coord, coord_axis, pixel_axis);
    casacore::Vector<casacore::String> units = _coord_sys.directionCoordinate().worldAxisUnits();
    angle.convert(units[coord_axis]);
    casacore::Vector<casacore::Double> increments(_coord_sys.directionCoordinate().increment());
    return fabs(angle.getValue() / increments[coord_axis]);
}

bool Region::CartaPointToWorld(const CARTA::Point& point, casacore::Vector<casacore::Quantity>& world_point) {
    // Convert CARTA point in pixel coordinates to world coordinates.
    // Returns world_point vector and boolean flag to indicate a successful conversion.
    bool converted(false);

    // Vectors must be same number of axes as in coord system for conversion:
    int naxes(_coord_sys.nPixelAxes());
    casacore::Vector<casacore::Double> pixel_coords(naxes), world_coords(naxes);
    pixel_coords = 0.0;
    pixel_coords(0) = point.x();
    pixel_coords(1) = point.y();

    // convert pixel vector to world vector
    if (_coord_sys.toWorld(world_coords, pixel_coords)) {
        casacore::Vector<casacore::String> world_units = _coord_sys.worldAxisUnits();
        world_point.resize(2);
        world_point(0) = casacore::Quantity(world_coords(0), world_units(0));
        world_point(1) = casacore::Quantity(world_coords(1), world_units(1));
        converted = true;
    }
    return converted;
}

bool Region::XyPixelsToWorld(casacore::Vector<casacore::Double> x_pixel, casacore::Vector<casacore::Double> y_pixel,
    casacore::Quantum<casacore::Vector<casacore::Double>>& x_world, casacore::Quantum<casacore::Vector<casacore::Double>>& y_world) {
    // Convert many points in pixel coordinates to world coordinates;
    // Returns world x and y vectors and boolean flag to indicate a successful conversion.
    bool converted(false);

    // convert pixel coords to world coords
    int num_axes(_coord_sys.nPixelAxes());
    int num_points(x_pixel.size());
    casacore::Matrix<casacore::Double> pixel_coords(num_axes, num_points);
    casacore::Matrix<casacore::Double> world_coords(num_axes, num_points);
    pixel_coords = 0.0;
    pixel_coords.row(0) = x_pixel;
    pixel_coords.row(1) = y_pixel;
    casacore::Vector<casacore::Bool> failures;
    if (_coord_sys.toWorldMany(world_coords, pixel_coords, failures)) {
        // make vectors of quantums for x and y
        casacore::Vector<casacore::String> world_units = _coord_sys.worldAxisUnits();
        x_world = world_coords.row(0);
        x_world.setUnit(world_units(0));
        y_world = world_coords.row(1);
        y_world.setUnit(world_units(1));
        converted = true;
    }
    return converted;
}
// *************************************************************************
// Parameter checking

bool Region::CheckPoints(const std::vector<CARTA::Point>& points) {
    // check point values
    bool points_ok(false);
    switch (_type) {
        case CARTA::POINT: {
            points_ok = CheckPixelPoint(points);
            break;
        }
        case CARTA::RECTANGLE: {
            points_ok = CheckRectanglePoints(points);
            break;
        }
        case CARTA::ELLIPSE: {
            points_ok = CheckEllipsePoints(points);
            break;
        }
        case CARTA::POLYGON: {
            points_ok = CheckPolygonPoints(points);
            break;
        }
        default:
            break;
    }
    return points_ok;
}

bool Region::CheckPixelPoint(const std::vector<CARTA::Point>& points) {
    // check if NaN or inf points
    bool points_ok(false);
    if (points.size() == 1) { // (x, y)
        float x(points[0].x()), y(points[0].y());
        points_ok = (std::isfinite(x) && std::isfinite(y));
    }
    return points_ok;
}

bool Region::CheckRectanglePoints(const std::vector<CARTA::Point>& points) {
    // check if NaN or inf points, width/height less than 0
    bool points_ok(false);
    if (points.size() == 2) { // [(cx,cy), (width,height)]
        float cx(points[0].x()), cy(points[0].y()), width(points[1].x()), height(points[1].y());
        bool points_exist = (std::isfinite(cx) && std::isfinite(cy) && std::isfinite(width) && std::isfinite(height));
        points_ok = (points_exist && (width > 0) && (height > 0));
    }
    return points_ok;
}

bool Region::CheckEllipsePoints(const std::vector<CARTA::Point>& points) {
    // check if NaN or inf points
    bool points_ok(false);
    if (points.size() == 2) { // [(cx,cy), (width,height)]
        float cx(points[0].x()), cy(points[0].y()), bmaj(points[1].x()), bmin(points[1].y());
        points_ok = (std::isfinite(cx) && std::isfinite(cy) && std::isfinite(bmaj) && std::isfinite(bmin));
    }
    return points_ok;
}

bool Region::CheckPolygonPoints(const std::vector<CARTA::Point>& points) {
    // check if NaN or inf points
    bool points_ok(true);
    for (auto& point : points)
        points_ok &= (std::isfinite(point.x()) && std::isfinite(point.y()));
    return points_ok;
}

bool Region::PointsChanged(const std::vector<CARTA::Point>& new_points) {
    // check equality of points (no operator==)
    bool changed(new_points.size() != _control_points.size()); // check number of points
    if (!changed) {                                            // check each point in vectors
        for (size_t i = 0; i < new_points.size(); ++i) {
            if ((new_points[i].x() != _control_points[i].x()) || (new_points[i].y() != _control_points[i].y())) {
                changed = true;
                break;
            }
        }
    }
    return changed;
}

// ***********************************
// Image Region with parameters applied

bool Region::GetRegion(casacore::ImageRegion& region, int stokes, ChannelRange channel_range) {
    // Return ImageRegion for given stokes and region parameters.
    bool region_ok(false);
    if (!IsValid() || (!_xy_region) || (stokes < 0))
        return region_ok;

    casacore::WCRegion* wc_region = MakeExtendedRegion(stokes, channel_range);
    if (wc_region != nullptr) {
        std::unique_lock<std::mutex> guard(_casacore_region_mutex);
        region = casacore::ImageRegion(wc_region);
        guard.unlock();
        region_ok = true;
    }
    return region_ok;
}

bool Region::SetXyRegion(const std::vector<CARTA::Point>& points, float rotation) {
    // create 2D casacore::WCRegion for type
    std::shared_ptr<const casacore::WCRegion> region;
    std::string region_type;
    try {
        switch (_type) {
            case CARTA::RegionType::POINT: {
                region_type = "POINT";
                region = std::shared_ptr<casacore::WCRegion>(MakePointRegion(points));
                break;
            }
            case CARTA::RegionType::RECTANGLE: {
                region_type = "RECTANGLE";
                region = std::shared_ptr<casacore::WCRegion>(MakeRectangleRegion(points, rotation));
                break;
            }
            case CARTA::RegionType::ELLIPSE: {
                region_type = "ELLIPSE";
                region = std::shared_ptr<casacore::WCRegion>(MakeEllipseRegion(points, rotation));
                break;
            }
            case CARTA::RegionType::POLYGON: {
                region_type = "POLYGON";
                region = std::shared_ptr<casacore::WCRegion>(MakePolygonRegion(points));
                break;
            }
            default:
                region_type = "NOT SUPPORTED";
                break;
        }
    } catch (casacore::AipsError& err) { // xy region failed
        std::cerr << "ERROR: xy region type " << region_type << " failed: " << err.getMesg() << std::endl;
    }
    std::atomic_store(&_xy_region, region);
    return bool(_xy_region);
}

casacore::WCRegion* Region::MakePointRegion(const std::vector<CARTA::Point>& points) {
    // 1 x 1 WCBox
    casacore::WCBox* box(nullptr);
    if (points.size() == 1) {
        // Convert point pixel coordinates to world coordinates;
        // This point will be blc and trc for WCBox
        casacore::Vector<casacore::Quantity> world_point;
        if (!CartaPointToWorld(points[0], world_point)) {
            return box; // nullptr, conversion failed
        }

        casacore::Vector<casacore::Int> abs_rel;
        std::unique_lock<std::mutex> guard(_casacore_region_mutex);
        box = new casacore::WCBox(world_point, world_point, _xy_axes, _coord_sys, abs_rel);
        guard.unlock();
    }
    return box;
}

casacore::WCRegion* Region::MakeRectangleRegion(const std::vector<CARTA::Point>& points, float rotation) {
    casacore::WCPolygon* box_polygon(nullptr);
    if (points.size() == 2) {
        // points are (cx, cy), (width, height)
        float center_x = points[0].x();
        float center_y = points[0].y();
        float width = points[1].x();
        float height = points[1].y();

        // 4 corner points
        int num_points(4);
        casacore::Vector<casacore::Double> x(num_points), y(num_points);
        if (rotation == 0.0f) {
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

        // Convert pixel coords to world coords
        casacore::Quantum<casacore::Vector<casacore::Double>> x_world, y_world;
        if (!XyPixelsToWorld(x, y, x_world, y_world)) {
            return box_polygon; // nullptr, conversion failed
        }

        std::unique_lock<std::mutex> guard(_casacore_region_mutex);
        box_polygon = new casacore::WCPolygon(x_world, y_world, _xy_axes, _coord_sys);
        guard.unlock();
    }
    return box_polygon;
}

casacore::WCRegion* Region::MakeEllipseRegion(const std::vector<CARTA::Point>& points, float rotation) {
    // WCEllipse from center x,y, bmaj, bmin, rotation
    casacore::WCEllipsoid* ellipse(nullptr);

    if (points.size() == 2) {
        // Convert ellipsoid center (point 0) pixel coords to world coords
        casacore::Vector<casacore::Quantity> center_world;
        if (!CartaPointToWorld(points[0], center_world)) {
            return ellipse; // nullptr, conversion failed
        }

        // Make Quantities for ellipsoid radii (point 1); major axis > minor axis.
        // rotation is in degrees from y-axis, ellipse rotation angle is in radians from x-axis;
        // adjust by 90 degrees unless swapped maj/min axes
        float bmaj(points[1].x()), bmin(points[1].y()), rotation_degrees;
        casacore::Quantity major_axis, minor_axis;
        if (bmaj > bmin) {
            major_axis = casacore::Quantity(bmaj , "pix");
            minor_axis = casacore::Quantity(bmin , "pix");
            rotation_degrees = rotation + 90.0;
        } else {
            major_axis = casacore::Quantity(bmin , "pix");
            minor_axis = casacore::Quantity(bmaj , "pix");
            rotation_degrees = rotation;
        }
        casacore::Quantity theta = casacore::Quantity(rotation_degrees * (M_PI / 180.0f), "rad");

        std::unique_lock<std::mutex> guard(_casacore_region_mutex);
        ellipse = new casacore::WCEllipsoid(
            center_world(0), center_world(1), major_axis, minor_axis, theta, _xy_axes(0), _xy_axes(1), _coord_sys);
        guard.unlock();
    }
    return ellipse;
}

casacore::WCRegion* Region::MakePolygonRegion(const std::vector<CARTA::Point>& points) {
    // npoints region
    casacore::WCPolygon* polygon(nullptr);

    // Convert pixel coords to world coords
    size_t npoints(points.size());
    casacore::Vector<casacore::Double> x_pixel(npoints), y_pixel(npoints);
    for (size_t i = 0; i < npoints; ++i) {
        x_pixel(i) = points[i].x();
        y_pixel(i) = points[i].y();
    }
    casacore::Quantum<casacore::Vector<casacore::Double>> x_world, y_world;
    if (!XyPixelsToWorld(x_pixel, y_pixel, x_world, y_world)) {
        return polygon; // nullptr, conversion failed
    }

    std::unique_lock<std::mutex> guard(_casacore_region_mutex);
    polygon = new casacore::WCPolygon(x_world, y_world, _xy_axes, _coord_sys);
    guard.unlock();
    return polygon;
}

bool Region::MakeExtensionBox(casacore::WCBox& extend_box, int stokes, ChannelRange channel_range) {
    // Create extension box for stored channel range and given stokes.
    // This can change for different profile/histogram/stats requirements so not stored
    bool extension_ok(false);
    if (_num_dims < 3) {
        return extension_ok; // not needed
    }

    try {
        double min_chan(channel_range.from), max_chan(channel_range.to);
        double all_channels = _image_shape(_spectral_axis);
        if (channel_range.from == ALL_CHANNELS) {
            min_chan = 0;
        }
        if (channel_range.to == ALL_CHANNELS) {
            max_chan = all_channels - 1;
        }
        assert((max_chan >= min_chan) && (all_channels > max_chan));
        if (max_chan < 0 || min_chan < 0) {
            std::cerr << "ERROR: max(" << max_chan << ") or min(" << min_chan << ") channel is negative!" << std::endl;
            return extension_ok; // false
        }

        // Convert pixel coordinates to world coordinates;
        // Must be same number of axes as in coord system
        int num_axes(_coord_sys.nPixelAxes());
        casacore::Vector<casacore::Double> blc_pixel(num_axes, 0.0);
        casacore::Vector<casacore::Double> trc_pixel(num_axes, 0.0);
        blc_pixel(_spectral_axis) = min_chan;
        trc_pixel(_spectral_axis) = max_chan;
        if (num_axes > 3) {
            blc_pixel(_stokes_axis) = stokes;
            trc_pixel(_stokes_axis) = stokes;
        }
        casacore::Vector<casacore::Double> blc_world(num_axes);
        casacore::Vector<casacore::Double> trc_world(num_axes);
        if (!(_coord_sys.toWorld(blc_world, blc_pixel) && _coord_sys.toWorld(trc_world, trc_pixel)))
            return extension_ok; // false, conversions failed

        // make blc, trc Quantities for extension box
        int num_extension_axes(_num_dims - 2);
        casacore::Vector<casacore::String> world_units = _coord_sys.worldAxisUnits();
        casacore::Vector<casacore::Quantum<casacore::Double>> blc(num_extension_axes);
        casacore::Vector<casacore::Quantum<casacore::Double>> trc(num_extension_axes);
        // channel quantities
        int chan_index(_spectral_axis - 2);
        blc(chan_index) = casacore::Quantity(blc_world(_spectral_axis), world_units(_spectral_axis));
        trc(chan_index) = casacore::Quantity(trc_world(_spectral_axis), world_units(_spectral_axis));
        if (num_extension_axes > 1) {
            // stokes quantities
            int stokes_index(_stokes_axis - 2);
            blc(stokes_index) = casacore::Quantity(blc_world(_stokes_axis), world_units(_stokes_axis));
            trc(stokes_index) = casacore::Quantity(trc_world(_stokes_axis), world_units(_stokes_axis));
        }

        // make extension box
        casacore::IPosition axes = (num_extension_axes == 1 ? casacore::IPosition(1, 2) : casacore::IPosition(2, 2, 3));
        casacore::Vector<casacore::Int> abs_rel;
        std::unique_lock<std::mutex> guard(_casacore_region_mutex);
        extend_box = casacore::WCBox(blc, trc, axes, _coord_sys, abs_rel);
        guard.unlock();
        extension_ok = true;
    } catch (casacore::AipsError& err) {
        std::cerr << "Extension box failed: " << err.getMesg() << std::endl;
    }
    return extension_ok;
}

casacore::WCRegion* Region::MakeExtendedRegion(int stokes, ChannelRange channel_range) {
    std::shared_ptr<const casacore::WCRegion> current_xy_region = std::atomic_load(&_xy_region);

    // Return 2D wcregion extended by chan, stokes; xyregion if 2D
    if (_num_dims == 2) {
        std::unique_lock<std::mutex> guard(_casacore_region_mutex);
        return current_xy_region->cloneRegion(); // copy: this ptr owned by ImageRegion
        guard.unlock();
    }

    casacore::WCExtension* region(nullptr);
    try {
        // create extension box for channel/stokes
        casacore::WCBox ext_box;
        if (!MakeExtensionBox(ext_box, stokes, channel_range))
            return region; // nullptr, extension box failed

        // apply extension box with extension axes to xy region
        std::unique_lock<std::mutex> guard(_casacore_region_mutex);
        region = new casacore::WCExtension(*current_xy_region, ext_box);
        guard.unlock();
    } catch (casacore::AipsError& err) {
        std::cerr << "ERROR: Region extension failed: " << err.getMesg() << std::endl;
    }
    return region;
}

// ***********************************
// Region bounds, mask

casacore::IPosition Region::XyShape() {
    // returns bounding box shape of xy region
    std::shared_ptr<const casacore::WCRegion> current_xy_region = std::atomic_load(&_xy_region);
    casacore::IPosition xy_shape;
    if (current_xy_region) {
        std::unique_lock<std::mutex> guard(_casacore_region_mutex);
        casacore::LCRegion* region = current_xy_region->toLCRegion(_coord_sys, _image_shape);
        guard.unlock();
        if (region != nullptr)
            xy_shape = region->shape().keepAxes(_xy_axes);
    }
    return xy_shape;
}

casacore::IPosition Region::XyOrigin() {
    // returns bottom-left position of bounding box of xy region
    std::shared_ptr<const casacore::WCRegion> current_xy_region = std::atomic_load(&_xy_region);
    casacore::IPosition xy_origin;
    if (current_xy_region) {
        std::unique_lock<std::mutex> guard(_casacore_region_mutex);
        auto extended_region = static_cast<casacore::LCExtension*>(current_xy_region->toLCRegion(_coord_sys, _image_shape));
        guard.unlock();
        if (extended_region != nullptr)
            xy_origin = extended_region->region().expand(casacore::IPosition(2, 0, 0));
    }
    return xy_origin;
}

std::shared_ptr<casacore::ArrayLattice<casacore::Bool>> Region::XyMask() {
    // returns boolean mask of xy region
    std::shared_ptr<const casacore::WCRegion> current_xy_region = std::atomic_load(&_xy_region);
    std::shared_ptr<casacore::ArrayLattice<casacore::Bool>> mask;

    if (current_xy_region) {
        // get extended region (or original region for points)
        std::unique_lock<std::mutex> guard(_casacore_region_mutex);
        auto lc_region = current_xy_region->toLCRegion(_coord_sys, _image_shape);
        guard.unlock();

        // get original region
        switch (_type) {
            case CARTA::POINT: {
                auto region = static_cast<const casacore::LCBox*>(lc_region);
                mask = std::make_shared<casacore::ArrayLattice<casacore::Bool>>(region->getMask());
                break;
            }
            case CARTA::RECTANGLE:
            case CARTA::POLYGON: {
                auto extended_region = static_cast<casacore::LCExtension*>(lc_region);
                auto region = static_cast<const casacore::LCPolygon&>(extended_region->region());
                mask = std::make_shared<casacore::ArrayLattice<casacore::Bool>>(region.getMask());
                break;
            }
            case CARTA::ELLIPSE: {
                auto extended_region = static_cast<casacore::LCExtension*>(lc_region);
                auto region = static_cast<const casacore::LCEllipsoid&>(extended_region->region());
                mask = std::make_shared<casacore::ArrayLattice<casacore::Bool>>(region.getMask());
                break;
            }
            default:
                break;
        }
    }

    std::atomic_store(&_xy_mask, mask);
    return std::atomic_load(&_xy_mask);
}

// ***********************************
// As annotation region for export

casacore::CountedPtr<const casa::AnnotationBase> Region::AnnotationRegion(bool pixel_coord) {
    // Return region as annotation region for export.
    // If pixel_coord=false but conversion fails, creates region in pixel coordinates; better than nothing
    casa::AnnRegion* ann_region(nullptr);
    if (!_control_points.empty()) {
        casacore::Vector<casacore::Stokes::StokesTypes> stokes_types = GetCoordSysStokesTypes();
        switch (_type) {
            case CARTA::POINT: {
                casacore::Quantity x = casacore::Quantity(_control_points[0].x(), "pix");
                casacore::Quantity y = casacore::Quantity(_control_points[0].y(), "pix");
                if (!pixel_coord) {
                    casacore::Vector<casacore::Quantity> world_point;
                    if (CartaPointToWorld(_control_points[0], world_point)) {
                        x = world_point(0);
                        y = world_point(1);
                    }
                }
                ann_region = new casa::AnnRectBox(x, y, x, y, _coord_sys, _image_shape, stokes_types);
                break;
            }
            case CARTA::RECTANGLE: {
                casacore::Quantity cx, cy, xwidth, ywidth;
                cx = casacore::Quantity(_control_points[0].x(), "pix");
                cy = casacore::Quantity(_control_points[0].y(), "pix");
                xwidth = casacore::Quantity(_control_points[1].x(), "pix");
                ywidth = casacore::Quantity(_control_points[1].y(), "pix");
                if (!pixel_coord) {
                    casacore::Vector<casacore::Quantity> world_point;
                    if (CartaPointToWorld(_control_points[0], world_point)) { // will use pixel coords if conversion fails
                        cx = world_point[0];
                        cy = world_point[1];
                        xwidth = _coord_sys.toWorldLength(_control_points[1].x(), 0);  // pixel axis 0
                        ywidth = _coord_sys.toWorldLength(_control_points[1].y(), 1);  // pixel axis 1
                    }
                }
                if (_rotation == 0.0) {
                    ann_region = new casa::AnnCenterBox(cx, cy, xwidth, ywidth, _coord_sys, _image_shape, stokes_types);
                } else {
                    casacore::Quantity position_angle(_rotation, "deg");
                    ann_region = new casa::AnnRotBox(cx, cy, xwidth, ywidth, position_angle, _coord_sys, _image_shape, stokes_types);
                }
                break;
            }
            case CARTA::POLYGON: {
                size_t npoints(_control_points.size());
                casacore::Vector<casacore::Quantity> x_coords(npoints), y_coords(npoints);
                for (size_t i = 0; i < npoints; ++i) {
                    x_coords(i) = casacore::Quantity(_control_points[i].x(), "pix");
                    y_coords(i) = casacore::Quantity(_control_points[i].y(), "pix");
                }
                if (!pixel_coord) {
                    casacore::Vector<casacore::Double> x_pixel(npoints), y_pixel(npoints);
                    for (size_t i = 0; i < npoints; ++i) {
                        x_pixel(i) = _control_points[i].x();
                        y_pixel(i) = _control_points[i].y();
                    }
                    casacore::Quantum<casacore::Vector<casacore::Double>> x_world, y_world;
                    if (XyPixelsToWorld(x_pixel, y_pixel, x_world, y_world)) {
                        // Unfortunately, constructors for WCPolygon and AnnPolygon differ;
                        // convert Quantum<Vector> to Vector<Quantum>
                        casacore::Vector<casacore::Double> x_values(x_world.getValue());
                        casacore::Unit x_unit(x_world.getUnit());
                        casacore::Vector<casacore::Double> y_values(y_world.getValue());
                        casacore::Unit y_unit(y_world.getUnit());
                        for (size_t i = 0; i < x_values.size(); ++i) {
                            x_coords(i) = casacore::Quantity(x_values[i], x_unit);
                            y_coords(i) = casacore::Quantity(y_values[i], y_unit);
                        }
                    }
                }
                ann_region = new casa::AnnPolygon(x_coords, y_coords, _coord_sys, _image_shape, stokes_types);
                break;
            }
            case CARTA::ELLIPSE: {
                casacore::Quantity cx, cy, bmaj, bmin;
                cx = casacore::Quantity(_control_points[0].x(), "pix");
                cy = casacore::Quantity(_control_points[0].y(), "pix");
                bmaj = casacore::Quantity(_control_points[1].x(), "pix");
                bmin = casacore::Quantity(_control_points[1].y(), "pix");
                if (!pixel_coord) {
                    casacore::Vector<casacore::Quantity> world_point;
                    if (CartaPointToWorld(_control_points[0], world_point)) { // will use pixel coords if conversion fails
                        cx = world_point[0];
                        cy = world_point[1];
                        bmaj = _coord_sys.toWorldLength(_control_points[1].x(), 0);  // pixel axis 0
                        bmin = _coord_sys.toWorldLength(_control_points[1].y(), 1);  // pixel axis 1
                    }
                }
                casacore::Quantity position_angle(_rotation, "deg");
                ann_region = new casa::AnnEllipse(cx, cy, bmaj, bmin, position_angle, _coord_sys, _image_shape, stokes_types);
                break;
            }
            default:
                break;
        }
    }
    if (ann_region != nullptr) {
        ann_region->setAnnotationOnly(false);
    }
    casacore::CountedPtr<const casa::AnnotationBase> annotation_region = casacore::CountedPtr<const casa::AnnotationBase>(ann_region);
    return annotation_region;
}

casacore::Vector<casacore::Stokes::StokesTypes> Region::GetCoordSysStokesTypes() {
    // basically, convert ints to stokes types in vector
    casacore::Vector<casacore::Int> istokes = _coord_sys.stokesCoordinate().stokes();
    casacore::Vector<casacore::Stokes::StokesTypes> stokes_types(istokes.size());
    for (size_t i = 0; i < istokes.size(); ++i) {
        stokes_types(i) = casacore::Stokes::type(istokes(i));
    }
    return stokes_types;
}

// ***********************************
// Region data

bool Region::GetData(std::vector<float>& data, casacore::ImageInterface<float>& image) {
    // fill data vector using region masked lattice (subimage)
    bool data_ok(false);
    casacore::IPosition image_shape = image.shape();
    if (image_shape.empty())
        return data_ok;

    data.resize(image_shape.product());
    casacore::Array<float> tmp(image_shape, data.data(), casacore::StorageInitPolicy::SHARE);
    try {
        image.doGetSlice(tmp, casacore::Slicer(casacore::IPosition(image_shape.size(), 0), image_shape));
        data_ok = true;
    } catch (casacore::AipsError& err) {
        data.clear();
    }
    return data_ok;
}

// ***********************************
// RegionStats

// histogram

bool Region::SetHistogramRequirements(const std::vector<CARTA::SetHistogramRequirements_HistogramConfig>& histogram_reqs) {
    if (_region_stats) {
        _region_stats->SetHistogramRequirements(histogram_reqs);
        return true;
    }
    return false;
}

CARTA::SetHistogramRequirements_HistogramConfig Region::GetHistogramConfig(int histogram_index) {
    if (_region_stats) {
        return _region_stats->GetHistogramConfig(histogram_index);
    }
    return CARTA::SetHistogramRequirements_HistogramConfig();
}

size_t Region::NumHistogramConfigs() {
    if (_region_stats) {
        return _region_stats->NumHistogramConfigs();
    }
    return 0;
}

bool Region::GetMinMax(int channel, int stokes, float& min_val, float& max_val) {
    if (_region_stats) {
        return _region_stats->GetMinMax(channel, stokes, min_val, max_val);
    }
    return false;
}

void Region::SetMinMax(int channel, int stokes, float min_val, float max_val) {
    if (_region_stats) {
        std::pair<float, float> vals = std::make_pair(min_val, max_val);
        _region_stats->SetMinMax(channel, stokes, vals);
    }
}

void Region::CalcMinMax(int channel, int stokes, const std::vector<float>& data, float& min_val, float& max_val) {
    if (_region_stats) {
        _region_stats->CalcMinMax(channel, stokes, data, min_val, max_val);
    }
}

bool Region::GetHistogram(int channel, int stokes, int num_bins, CARTA::Histogram& histogram) {
    if (_region_stats) {
        return _region_stats->GetHistogram(channel, stokes, num_bins, histogram);
    }
    return false;
}

void Region::SetHistogram(int channel, int stokes, CARTA::Histogram& histogram) {
    if (_region_stats) {
        _region_stats->SetHistogram(channel, stokes, histogram);
    }
}

void Region::CalcHistogram(
    int channel, int stokes, int num_bins, float min_val, float max_val, const std::vector<float>& data, CARTA::Histogram& histogram_msg) {
    if (_region_stats) {
        _region_stats->CalcHistogram(channel, stokes, num_bins, min_val, max_val, data, histogram_msg);
    }
}

// stats
void Region::SetStatsRequirements(const std::vector<int>& stats_types) {
    if (_region_stats) {
        _region_stats->SetStatsRequirements(stats_types);
    }
}

size_t Region::NumStats() {
    if (_region_stats) {
        return _region_stats->NumStats();
    }
    return 0;
}

void Region::FillStatsData(CARTA::RegionStatsData& stats_data, const casacore::ImageInterface<float>& image, int channel, int stokes) {
    if (_region_stats) {
        _region_stats->FillStatsData(stats_data, image, channel, stokes);
    }
}

void Region::FillStatsData(CARTA::RegionStatsData& stats_data, std::map<CARTA::StatsType, double>& stats_values) {
    if (_region_stats) {
        _region_stats->FillStatsData(stats_data, stats_values);
    }
}

void Region::FillNaNStatsData(CARTA::RegionStatsData& stats_data) {
    // set stats 0-9 with NaN values when no subimage (region is outside image)
    for (int i = CARTA::StatsType::NumPixels; i < CARTA::StatsType::Blc; ++i) {
        auto carta_stats_type = static_cast<CARTA::StatsType>(i);
        double nan_value = std::numeric_limits<double>::quiet_NaN();
        if (carta_stats_type == CARTA::StatsType::NanCount) { // not implemented
            continue;
        }
        if (carta_stats_type == CARTA::StatsType::NumPixels) {
            nan_value = 0.0;
        }
        auto stats_value = stats_data.add_statistics();
        stats_value->set_stats_type(carta_stats_type);
        stats_value->set_value(nan_value);
    }
}

// ***********************************
// RegionProfiler

void Region::SetAllProfilesUnsent() {
    if (_region_profiler) {
        _region_profiler->SetAllSpatialProfilesUnsent();
        _region_profiler->SetAllSpectralProfilesUnsent();
    }
}

// spatial

bool Region::SetSpatialRequirements(const std::vector<std::string>& profiles, const int num_stokes) {
    if (_region_profiler) {
        return _region_profiler->SetSpatialRequirements(profiles, num_stokes);
    }
    return false;
}

size_t Region::NumSpatialProfiles() {
    if (_region_profiler) {
        return _region_profiler->NumSpatialProfiles();
    }
    return 0;
}

std::string Region::GetSpatialCoordinate(int profile_index) {
    if (_region_profiler) {
        return _region_profiler->GetSpatialCoordinate(profile_index);
    }
    return std::string();
}

std::pair<int, int> Region::GetSpatialProfileAxes(int profile_index) {
    if (_region_profiler) {
        return _region_profiler->GetSpatialProfileAxes(profile_index);
    }
    return std::make_pair(-1, -1);
}

bool Region::GetSpatialProfileSent(int profile_index) {
    if (_region_profiler) {
        return _region_profiler->GetSpatialProfileSent(profile_index);
    }
    return false;
}

void Region::SetSpatialProfileSent(int profile_index, bool sent) {
    if (_region_profiler) {
        _region_profiler->SetSpatialProfileSent(profile_index, sent);
    }
}

// spectral

bool Region::SetSpectralRequirements(const std::vector<CARTA::SetSpectralRequirements_SpectralConfig>& configs, const int num_stokes) {
    if (_region_profiler) {
        return _region_profiler->SetSpectralRequirements(configs, num_stokes);
    }
    return false;
}

size_t Region::NumSpectralProfiles() {
    if (_region_profiler) {
        return _region_profiler->NumSpectralProfiles();
    }
    return 0;
}

int Region::NumStatsToLoad(int profile_index) {
    if (_region_profiler) {
        return _region_profiler->NumStatsToLoad(profile_index);
    }
    return 0;
}

bool Region::GetSpectralConfigStats(int profile_index, std::vector<int>& stats) {
    if (_region_profiler) {
        return _region_profiler->GetSpectralConfigStats(profile_index, stats);
    }
    return false;
}

bool Region::GetSpectralStatsToLoad(int profile_index, std::vector<int>& stats) {
    if (_region_profiler) {
        return _region_profiler->GetSpectralStatsToLoad(profile_index, stats);
    }
    return false;
}

bool Region::GetSpectralProfileStatSent(int profile_index, int stats_type) {
    if (_region_profiler) {
        return _region_profiler->GetSpectralProfileStatSent(profile_index, stats_type);
    }
    return false;
}

void Region::SetSpectralProfileStatSent(int profile_index, int stats_type, bool sent) {
    if (_region_profiler) {
        _region_profiler->SetSpectralProfileStatSent(profile_index, stats_type, sent);
    }
}

void Region::SetSpectralProfileAllStatsSent(int profile_index, bool sent) {
    // for fixed stokes profiles when stokes changed, do not resend
    if (_region_profiler) {
        _region_profiler->SetSpectralProfileAllStatsSent(profile_index, sent);
    }
}

int Region::GetSpectralConfigStokes(int profile_index) {
    if (_region_profiler) {
        return _region_profiler->GetSpectralConfigStokes(profile_index);
    }
    return CURRENT_STOKES - 1; // invalid
}

std::string Region::GetSpectralCoordinate(int profile_index) {
    if (_region_profiler) {
        return _region_profiler->GetSpectralCoordinate(profile_index);
    }
    return std::string();
}

bool Region::GetSpectralProfileData(
    std::vector<std::vector<double>>& stats_values, int profile_index, casacore::ImageInterface<float>& image) {
    // Get SpectralProfile with statistics values to load according to config stored in RegionProfiler
    bool have_stats(false);
    std::vector<int> required_stats;
    if (GetSpectralStatsToLoad(profile_index, required_stats)) {
        if ((required_stats.size() > 0) && _region_stats) { // get required stats values
            have_stats = _region_stats->CalcStatsValues(stats_values, required_stats, image);
        }
    }
    return have_stats;
}

void Region::FillPointSpectralProfileData(CARTA::SpectralProfileData& profile_data, int profile_index, std::vector<float>& spectral_data) {
    // Fill SpectralProfile with values for point region; assumes one spectral config with StatsType::Sum
    if (IsPoint()) {
        CARTA::StatsType type = CARTA::StatsType::Sum;
        if (!GetSpectralProfileStatSent(profile_index, type)) {
            std::string profile_coord(GetSpectralCoordinate(profile_index));
            auto new_profile = profile_data.add_profiles();
            new_profile->set_coordinate(profile_coord);
            new_profile->set_stats_type(type);
            new_profile->set_raw_values_fp32(spectral_data.data(), spectral_data.size() * sizeof(float));
            if (profile_data.progress() == PROFILE_COMPLETE) {
                SetSpectralProfileStatSent(profile_index, type, true);
            }
        }
    }
}

void Region::FillSpectralProfileData(
    CARTA::SpectralProfileData& profile_data, int profile_index, std::map<CARTA::StatsType, std::vector<double>>& stats_values) {
    // Fill SpectralProfile with statistics values according to config stored in RegionProfiler
    // using values calculated externally and passed in as a parameter
    std::vector<int> required_stats;
    if (GetSpectralStatsToLoad(profile_index, required_stats)) {
        std::string profile_coord(GetSpectralCoordinate(profile_index));
        for (size_t i = 0; i < required_stats.size(); ++i) {
            // one SpectralProfile per stats type
            auto new_profile = profile_data.add_profiles();
            new_profile->set_coordinate(profile_coord);
            auto stat_type = static_cast<CARTA::StatsType>(required_stats[i]);
            new_profile->set_stats_type(stat_type);
            if (stats_values.find(stat_type) == stats_values.end()) { // stat not provided
                double nan_value = std::numeric_limits<double>::quiet_NaN();
                new_profile->set_raw_values_fp64(&nan_value, sizeof(double));
            } else {
                new_profile->set_raw_values_fp64(stats_values[stat_type].data(), stats_values[stat_type].size() * sizeof(double));
            }
            if (profile_data.progress() == PROFILE_COMPLETE) {
                SetSpectralProfileStatSent(profile_index, stat_type, true);
            }
        }
    }
}

// TODO: This function can be replaced by the upper one and removed in the future.
void Region::FillSpectralProfileData(
    CARTA::SpectralProfileData& profile_data, int profile_index, const std::vector<std::vector<double>>& stats_values) {
    // Fill SpectralProfile with statistics values according to config stored in RegionProfiler
    std::vector<int> required_stats;
    if (GetSpectralStatsToLoad(profile_index, required_stats)) {
        std::string profile_coord(GetSpectralCoordinate(profile_index));
        for (size_t i = 0; i < required_stats.size(); ++i) {
            // one SpectralProfile per stats type
            auto new_profile = profile_data.add_profiles();
            new_profile->set_coordinate(profile_coord);
            auto stat_type = static_cast<CARTA::StatsType>(required_stats[i]);
            new_profile->set_stats_type(stat_type);
            if (stats_values[i].empty()) { // region outside image or NaNs
                double nan_value = std::numeric_limits<double>::quiet_NaN();
                new_profile->set_raw_values_fp64(&nan_value, sizeof(double));
            } else {
                new_profile->set_raw_values_fp64(stats_values[i].data(), stats_values[i].size() * sizeof(double));
            }
            if (profile_data.progress() == PROFILE_COMPLETE) {
                SetSpectralProfileStatSent(profile_index, stat_type, true);
            }
        }
    }
}

void Region::FillNaNSpectralProfileData(CARTA::SpectralProfileData& profile_data, int profile_index) {
    // Fill spectral profile with NaN statistics values according to config stored in RegionProfiler
    std::vector<int> required_stats;
    if (GetSpectralStatsToLoad(profile_index, required_stats)) {
        std::string profile_coord(GetSpectralCoordinate(profile_index));
        for (size_t i = 0; i < required_stats.size(); ++i) {
            // one SpectralProfile per stats type
            auto new_profile = profile_data.add_profiles();
            new_profile->set_coordinate(profile_coord);
            auto stat_type = static_cast<CARTA::StatsType>(required_stats[i]);
            new_profile->set_stats_type(stat_type);
            // region outside image or NaNs
            double nan_value = std::numeric_limits<double>::quiet_NaN();
            new_profile->set_raw_values_fp64(&nan_value, sizeof(double));
            SetSpectralProfileStatSent(profile_index, stat_type, true);
        }
    }
}

void Region::SetConnectionFlag(bool connected) {
    _connected = connected;
}

bool Region::IsConnected() {
    return _connected;
}

void Region::DisconnectCalled() {
    SetConnectionFlag(false); // set a false flag to interrupt the running jobs in the Region
    while (_z_profile_count) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } // wait for the jobs finished
}
