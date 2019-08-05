//# Region.cc: implementation of class for managing a region

#include "Region.h"

#include <algorithm> // max
#include <cmath>     // round
#include <stdio.h>   // sscanf

#include <casacore/casa/Arrays/ArrayLogical.h>
#include <casacore/images/Regions/WCEllipsoid.h>
#include <casacore/images/Regions/WCExtension.h>
#include <casacore/images/Regions/WCPolygon.h>
#include <casacore/images/Regions/WCRegion.h>
#include <casacore/lattices/LRegions/LCBox.h>
#include <casacore/lattices/LRegions/LCEllipsoid.h>
#include <casacore/lattices/LRegions/LCExtension.h>
#include <casacore/lattices/LRegions/LCPolygon.h>
#include <casacore/casa/Quanta/Quantum.h>

#include <imageanalysis/Annotations/AnnCircle.h>
#include <imageanalysis/Annotations/AnnEllipse.h>
#include <imageanalysis/Annotations/AnnRegion.h>
#include <imageanalysis/Annotations/AnnRotBox.h>
#include <imageanalysis/Annotations/AnnPolygon.h>

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
      _xy_region(nullptr),
      _xy_mask(nullptr),
      _coord_sys(coord_sys) {
    // validate and set region parameters
    _num_dims = image_shape.size();
    _valid = UpdateRegionParameters(name, type, points, rotation);
    if (_valid) {
        _region_stats = std::unique_ptr<RegionStats>(new RegionStats());
        _region_profiler = std::unique_ptr<RegionProfiler>(new RegionProfiler());
    }
}

Region::Region(casacore::CountedPtr<const casa::AnnotationBase> annotation_region, std::map<casa::AnnotationBase::Keyword,
    casacore::String>& globals, const casacore::IPosition image_shape, int spectral_axis, int stokes_axis,
    const casacore::CoordinateSystem& coord_sys)
    : _rotation(0.0),
      _valid(false),
      _image_shape(image_shape),
      _spectral_axis(spectral_axis),
      _stokes_axis(stokes_axis),
      _xy_axes(casacore::IPosition(2, 0, 1)),
      _xy_region(nullptr),
      _xy_mask(nullptr),
      _coord_sys(coord_sys) {
    // Create region from imported annotation region:
    // set name, type, control points, rotation (default 0.0 already set), and xy region
    if (annotation_region) {
        _name = annotation_region->getLabel();
        //casacore::CoordinateSystem region_coord_sys = annotation_region->getCsys();
        switch (annotation_region->getType()) {
            case casa::AnnotationBase::RECT_BOX:
            case casa::AnnotationBase::CENTER_BOX: {
                // all rectangles are polygons
                const casa::AnnPolygon* polygon = static_cast<const casa::AnnPolygon*>(annotation_region.get());
                if (polygon != nullptr) {
                    _xy_region = polygon->getRegion2().get()->cloneRegion();
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
                    _xy_region = polygon->getRegion2().get()->cloneRegion();
                    // get polygon vertices for control points
                    std::vector<casacore::Double> x, y;
                    polygon->pixelVertices(x, y);
                    for (size_t i=0; i < x.size(); ++i) {
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
                casacore::Vector<casacore::Double> increments = dir_coord.increment();
		bool circle_is_ellipse(false);
		if ((ann_type == casa::AnnotationBase::CIRCLE) && dir_coord.hasSquarePixels()) {
                    const casa::AnnCircle* circle = static_cast<const casa::AnnCircle*>(annotation_region.get());
                    if (circle != nullptr) {
                        _xy_region = circle->getRegion2().get()->cloneRegion();
                        // set control point: cx, cy
                        casacore::MDirection center_position = circle->getCenter();
                        casacore::Vector<casacore::Double> pixel_coords;
			dir_coord.toPixel(pixel_coords, center_position);
			CARTA::Point point;
			point.set_x(pixel_coords[0]);
			point.set_y(pixel_coords[1]);
                        _control_points.push_back(point);
                        // set control point: bmaj, bmin
                        casacore::Quantity radius = circle->getRadius();
			double radius_pix = radius.getValue() / increments(0);
			point.set_x(radius_pix);
			point.set_y(radius_pix);
                        _control_points.push_back(point);
                        _valid = true;
                    }
                } else {
                    circle_is_ellipse = true;
                }

                // if pixels not square, circle is an AnnEllipse
		if ((ann_type == casa::AnnotationBase::ELLIPSE) || circle_is_ellipse) {
                    const casa::AnnEllipse* ellipse = static_cast<const casa::AnnEllipse*>(annotation_region.get());
                    if (ellipse != nullptr) {
                        _xy_region = ellipse->getRegion2().get()->cloneRegion();
                        // set control point: cx, cy
                        casacore::MDirection center_position = ellipse->getCenter();
                        casacore::Vector<casacore::Double> pixel_coords;
			dir_coord.toPixel(pixel_coords, center_position);
			CARTA::Point point;
			point.set_x(pixel_coords[0]);
			point.set_y(pixel_coords[1]);
                        _control_points.push_back(point);
                        // set control point: bmaj, bmin
                        casacore::Quantity bmaj = ellipse->getSemiMajorAxis();
			double bmaj_pix = bmaj.getValue() / increments(0);
                        casacore::Quantity bmin = ellipse->getSemiMinorAxis();
			double bmin_pix = bmin.getValue() / increments(1);
			point.set_x(bmaj_pix);
			point.set_y(bmin_pix);
                        _control_points.push_back(point);
                        // set rotation
                        casacore::Quantity position_angle = ellipse->getPositionAngle();
			position_angle.convert("deg");
			_rotation = position_angle.getValue();
                        _valid = true;
                    }
                }
                break;
            }
            case casa::AnnotationBase::POLYLINE:
            case casa::AnnotationBase::ANNULUS:
            default:
                break;
        }
    }
}

Region::~Region() {
    if (_xy_region) {
        delete _xy_region;
        _xy_region = nullptr;
    }
    if (_xy_mask) {
        delete _xy_mask;
        _xy_mask = nullptr;
    }
    _region_stats.reset();
    _region_profiler.reset();
}

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
    if (!IsValid() || (_xy_region == nullptr) || (stokes < 0))
        return region_ok;

    casacore::WCRegion* wc_region = MakeExtendedRegion(stokes, channel_range);
    if (wc_region != nullptr) {
        region = casacore::ImageRegion(wc_region);
        region_ok = true;
    }
    return region_ok;
}

bool Region::SetXyRegion(const std::vector<CARTA::Point>& points, float rotation) {
    // create 2D casacore::WCRegion for type
    casacore::WCRegion* region(nullptr);
    std::string region_type;
    try {
        switch (_type) {
            case CARTA::RegionType::POINT: {
                region_type = "POINT";
                region = MakePointRegion(points);
                break;
            }
            case CARTA::RegionType::RECTANGLE: {
                region_type = "RECTANGLE";
                region = MakeRectangleRegion(points, rotation);
                break;
            }
            case CARTA::RegionType::ELLIPSE: {
                region_type = "ELLIPSE";
                region = MakeEllipseRegion(points, rotation);
                break;
            }
            case CARTA::RegionType::POLYGON: {
                region_type = "POLYGON";
                region = MakePolygonRegion(points);
                break;
            }
            default:
                region_type = "NOT SUPPORTED";
                break;
        }
    } catch (casacore::AipsError& err) { // xy region failed
        std::cerr << "ERROR: xy region type " << region_type << " failed: " << err.getMesg() << std::endl;
    }
    delete _xy_region;
    _xy_region = region;
    return (_xy_region != nullptr);
}

casacore::WCRegion* Region::MakePointRegion(const std::vector<CARTA::Point>& points) {
    // 1 x 1 WCBox
    casacore::WCBox* box(nullptr);
    if (points.size() == 1) {
        auto x = points[0].x();
        auto y = points[0].y();

        // Convert pixel coordinates to world coordinates;
        // Must be same number of axes as in coord system
        int naxes(_coord_sys.nPixelAxes());
        casacore::Vector<casacore::Double> pixel_coords(naxes);
        casacore::Vector<casacore::Double> world_coords(naxes);
        pixel_coords = 0.0;
        pixel_coords(0) = x;
        pixel_coords(1) = y;
        if (!_coord_sys.toWorld(world_coords, pixel_coords))
            return box; // nullptr, conversion failed

        // make blc quantities (trc=blc for point)
        casacore::Vector<casacore::String> coord_units = _coord_sys.worldAxisUnits();
        casacore::Vector<casacore::Quantum<casacore::Double>> blc(2);
        blc(0) = casacore::Quantity(world_coords(0), coord_units(0));
        blc(1) = casacore::Quantity(world_coords(1), coord_units(1));
        // using pixel axes 0 and 1
        casacore::IPosition axes(2, 0, 1);
        casacore::Vector<casacore::Int> abs_rel;
        box = new casacore::WCBox(blc, blc, axes, _coord_sys, abs_rel);
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
        int num_axes(_coord_sys.nPixelAxes());
        casacore::Matrix<casacore::Double> pixel_coords(num_axes, num_points);
        casacore::Matrix<casacore::Double> world_coords(num_axes, num_points);
        pixel_coords = 0.0;
        pixel_coords.row(0) = x;
        pixel_coords.row(1) = y;
        casacore::Vector<casacore::Bool> failures;
        if (!_coord_sys.toWorldMany(world_coords, pixel_coords, failures))
            return box_polygon; // nullptr, conversion failed

        // make a vector of quantums for x and y
        casacore::Quantum<casacore::Vector<casacore::Double>> x_coord(world_coords.row(0));
        casacore::Quantum<casacore::Vector<casacore::Double>> y_coord(world_coords.row(1));
        casacore::Vector<casacore::String> coord_units = _coord_sys.worldAxisUnits();
        x_coord.setUnit(coord_units(0));
        y_coord.setUnit(coord_units(1));

        box_polygon = new casacore::WCPolygon(x_coord, y_coord, _xy_axes, _coord_sys);
    }
    return box_polygon;
}

casacore::WCRegion* Region::MakeEllipseRegion(const std::vector<CARTA::Point>& points, float rotation) {
    // WCEllipse from center x,y, bmaj, bmin, rotation
    casacore::WCEllipsoid* ellipse(nullptr);

    if (points.size() == 2) {
        float cx(points[0].x()), cy(points[0].y());
        float bmaj(points[1].x()), bmin(points[1].y());
        // rotation is in degrees from y-axis;
        // ellipse rotation angle is in radians from x-axis
        float theta;

        // Convert ellipsoid center pixel coords to world coords
        int num_axes(_coord_sys.nPixelAxes());
        casacore::Vector<casacore::Double> pixel_coords(num_axes);
        casacore::Vector<casacore::Double> world_coords(num_axes);
        pixel_coords = 0.0;
        pixel_coords(0) = cx;
        pixel_coords(1) = cy;
        if (!_coord_sys.toWorld(world_coords, pixel_coords)) {
            return ellipse; // nullptr, conversion failed
        }

        // make Quantities for ellipsoid center
        casacore::Vector<casacore::String> coord_units = _coord_sys.worldAxisUnits();
        casacore::Vector<casacore::Quantum<casacore::Double>> center(2);
        center(0) = casacore::Quantity(world_coords(0), coord_units(0));
        center(1) = casacore::Quantity(world_coords(1), coord_units(1));

        // make Quantities for ellipsoid radii
        casacore::Vector<casacore::Quantum<casacore::Double>> radii(2);
        radii(0) = casacore::Quantity(bmaj, "pix");
        radii(1) = casacore::Quantity(bmin, "pix");

        // Make sure the major axis is greater than the minor axis
        casacore::Quantity major_axis;
        casacore::Quantity minor_axis;
        if (radii(0) < radii(1)) {
            major_axis = radii(1);
            minor_axis = radii(0);
            theta = (rotation) * (M_PI / 180.0f);
        } else {
            major_axis = radii(0);
            minor_axis = radii(1);
            theta = (rotation + 90.0) * (M_PI / 180.0f);
        }

        // Convert theta to a Quantity
        casacore::Quantity quantity_theta = casacore::Quantity(static_cast<double>(theta), "rad");

        ellipse =
            new casacore::WCEllipsoid(center(0), center(1), major_axis, minor_axis, quantity_theta, _xy_axes(0), _xy_axes(1), _coord_sys);
    }
    return ellipse;
}

casacore::WCRegion* Region::MakePolygonRegion(const std::vector<CARTA::Point>& points) {
    // npoints region
    casacore::WCPolygon* polygon(nullptr);
    size_t num_points(points.size());
    casacore::Vector<casacore::Double> x(num_points), y(num_points);
    for (size_t i = 0; i < num_points; ++i) {
        x(i) = points[i].x();
        y(i) = points[i].y();
    }

    // Convert pixel coords to world coords
    int num_axes(_coord_sys.nPixelAxes());
    casacore::Matrix<casacore::Double> pixel_coords(num_axes, num_points);
    casacore::Matrix<casacore::Double> world_coords(num_axes, num_points);
    pixel_coords = 0.0;
    pixel_coords.row(0) = x;
    pixel_coords.row(1) = y;
    casacore::Vector<casacore::Bool> failures;
    if (!_coord_sys.toWorldMany(world_coords, pixel_coords, failures)) {
        return polygon; // nullptr, conversion failed
    }

    // make a vector of quantums for x and y
    casacore::Quantum<casacore::Vector<casacore::Double>> x_coord(world_coords.row(0));
    casacore::Quantum<casacore::Vector<casacore::Double>> y_coord(world_coords.row(1));
    casacore::Vector<casacore::String> coord_units = _coord_sys.worldAxisUnits();
    x_coord.setUnit(coord_units(0));
    y_coord.setUnit(coord_units(1));

    polygon = new casacore::WCPolygon(x_coord, y_coord, _xy_axes, _coord_sys);
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
        casacore::Vector<casacore::Double> blc_pixel_coords(num_axes, 0.0);
        casacore::Vector<casacore::Double> trc_pixel_coords(num_axes, 0.0);
        casacore::Vector<casacore::Double> blc_world_coords(num_axes);
        casacore::Vector<casacore::Double> trc_world_coords(num_axes);
        blc_pixel_coords(_spectral_axis) = min_chan;
        trc_pixel_coords(_spectral_axis) = max_chan;
        if (num_axes > 3) {
            blc_pixel_coords(_stokes_axis) = stokes;
            trc_pixel_coords(_stokes_axis) = stokes;
        }
        if (!(_coord_sys.toWorld(blc_world_coords, blc_pixel_coords) && _coord_sys.toWorld(trc_world_coords, trc_pixel_coords)))
            return extension_ok; // false, conversions failed

        // make blc, trc Quantities
        int num_extension_axes(_num_dims - 2);
        casacore::Vector<casacore::String> coord_units = _coord_sys.worldAxisUnits();
        casacore::Vector<casacore::Quantum<casacore::Double>> blc(num_extension_axes);
        casacore::Vector<casacore::Quantum<casacore::Double>> trc(num_extension_axes);
        // channel quantities
        int chan_index(_spectral_axis - 2);
        blc(chan_index) = casacore::Quantity(blc_world_coords(_spectral_axis), coord_units(_spectral_axis));
        trc(chan_index) = casacore::Quantity(trc_world_coords(_spectral_axis), coord_units(_spectral_axis));
        if (num_extension_axes > 1) {
            // stokes quantities
            int stokes_index(_stokes_axis - 2);
            blc(stokes_index) = casacore::Quantity(blc_world_coords(_stokes_axis), coord_units(_stokes_axis));
            trc(stokes_index) = casacore::Quantity(trc_world_coords(_stokes_axis), coord_units(_stokes_axis));
        }

        // make extension box
        casacore::IPosition axes = (num_extension_axes == 1 ? casacore::IPosition(1, 2) : casacore::IPosition(2, 2, 3));
        casacore::Vector<casacore::Int> abs_rel;
        extend_box = casacore::WCBox(blc, trc, axes, _coord_sys, abs_rel);
        extension_ok = true;
    } catch (casacore::AipsError& err) {
        std::cerr << "Extension box failed: " << err.getMesg() << std::endl;
    }
    return extension_ok;
}

casacore::WCRegion* Region::MakeExtendedRegion(int stokes, ChannelRange channel_range) {
    // Return 2D wcregion extended by chan, stokes; xyregion if 2D
    if (_num_dims == 2) {
        return _xy_region->cloneRegion(); // copy: this ptr owned by ImageRegion
    }

    casacore::WCExtension* region(nullptr);
    try {
        // create extension box for channel/stokes
        casacore::WCBox ext_box;
        if (!MakeExtensionBox(ext_box, stokes, channel_range))
            return region; // nullptr, extension box failed

        // apply extension box with extension axes to xy region
        region = new casacore::WCExtension(*_xy_region, ext_box);
    } catch (casacore::AipsError& err) {
        std::cerr << "ERROR: Region extension failed: " << err.getMesg() << std::endl;
    }
    return region;
}

// ***********************************
// Region bounds, mask

casacore::IPosition Region::XyShape() {
    // returns bounding box shape of xy region
    casacore::IPosition xy_shape;
    if (_xy_region != nullptr) {
        casacore::LCRegion* region = _xy_region->toLCRegion(_coord_sys, _image_shape);
        if (region != nullptr)
            xy_shape = region->shape().keepAxes(_xy_axes);
    }
    return xy_shape;
}

casacore::IPosition Region::XyOrigin() {
    // returns bottom-left position of bounding box of xy region
    casacore::IPosition xy_origin;
    if (_xy_region != nullptr) {
        auto extended_region = static_cast<casacore::LCExtension*>(_xy_region->toLCRegion(_coord_sys, _image_shape));
        if (extended_region != nullptr)
            xy_origin = extended_region->region().expand(casacore::IPosition(2, 0, 0));
    }
    return xy_origin;
}

const casacore::ArrayLattice<casacore::Bool>* Region::XyMask() {
    // returns boolean mask of xy region
    casacore::ArrayLattice<casacore::Bool>* mask;

    if (_xy_region != nullptr) {
        // get extended region (or original region for points)
        auto lc_region = _xy_region->toLCRegion(_coord_sys, _image_shape);

        // get original region
        switch (_type) {
            case CARTA::POINT: {
                auto region = static_cast<const casacore::LCBox*>(lc_region);
                mask = new casacore::ArrayLattice<casacore::Bool>(region->getMask());
                break;
            }
            case CARTA::RECTANGLE:
            case CARTA::POLYGON: {
                auto extended_region = static_cast<casacore::LCExtension*>(lc_region);
                auto region = static_cast<const casacore::LCPolygon&>(extended_region->region());
                mask = new casacore::ArrayLattice<casacore::Bool>(region.getMask());
                break;
            }
            case CARTA::ELLIPSE: {
                auto extended_region = static_cast<casacore::LCExtension*>(lc_region);
                auto region = static_cast<const casacore::LCEllipsoid&>(extended_region->region());
                mask = new casacore::ArrayLattice<casacore::Bool>(region.getMask());
                break;
            }
            default:
                break;
        }
    }

    if (_xy_mask) {
        delete _xy_mask;
    }
    _xy_mask = mask;

    return _xy_mask;
}

// ***********************************
// As annotation region for export

casacore::CountedPtr<const casa::AnnotationBase> Region::AnnotationRegion() {
    // return region as annotation region for export
    casa::AnnRegion* ann_region(nullptr);
    switch (_type) {
        case CARTA::POINT: {
            //casacore::WCBox
            //TODO ann_region = new casa::AnnRectBox();
            break;
        }
        case CARTA::RECTANGLE: {
            /* TODO
            //casacore::WCPolygon
            if (_rotation == 0.0) {
                ann_region = new casa::AnnCenterBox();
            } else {
                ann_region = new casa::AnnRotBox();
            }
            */
            break;
        }
        case CARTA::POLYGON: {
            //casacore::WCPolygon
            //TODO ann_region = new casa::AnnPolygon();
            break;
        }
        case CARTA::ELLIPSE: {
            //casacore::WCEllipse
            //TODO ann_region = new casa::AnnEllipse();
            break;
        }
        default:
            break;
    }
    casacore::CountedPtr<const casa::AnnotationBase> annotation_region = casacore::CountedPtr<const casa::AnnotationBase>(ann_region);
    return annotation_region;
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
