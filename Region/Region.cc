//# Region.cc: implementation of class for managing a region

#include "Region.h"

#include <stdio.h>   // sscanf
#include <algorithm> // max
#include <cmath>     // round

#include <casacore/casa/Arrays/ArrayLogical.h>
#include <casacore/casa/Quanta/QMath.h>
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
#include <imageanalysis/Annotations/AnnSymbol.h>

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
      _coord_sys(coord_sys),
      _z_profile_count(0) {
    // Create region from control points and rotation
    _num_dims = image_shape.size();
    // validate and set region parameters
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
      _coord_sys(coord_sys),
      _z_profile_count(0) {
    // Create region from imported annotation region
    _num_dims = image_shape.size();
    // set name, type, control points, rotation (default 0.0 already set), and xy region
    if (annotation_region) {
        try {
            switch (annotation_region->getType()) {
                case casa::AnnotationBase::RECT_BOX:
                case casa::AnnotationBase::CENTER_BOX: {
                    _name = annotation_region->getLabel();
                    _type = CARTA::RegionType::RECTANGLE;
                    // all rectangles are polygons
                    const casa::AnnPolygon* polygon = dynamic_cast<const casa::AnnPolygon*>(annotation_region.get());
                    if (polygon != nullptr) {
                        // store WCRegion
                        std::atomic_store(&_xy_region, polygon->getRegion2());

                        // get polygon pixel vertices, control points
                        double cx_pix, cy_pix, width_pix, height_pix;
                        std::vector<casacore::Double> x, y;
                        polygon->pixelVertices(x, y);
                        GetRectangleControlPointsFromVertices(x, y, cx_pix, cy_pix, width_pix, height_pix);
                        CARTA::Point point;
                        point.set_x(cx_pix);
                        point.set_y(cy_pix);
                        _control_points.push_back(point);
                        point.set_x(width_pix);
                        point.set_y(height_pix);
                        _control_points.push_back(point);

                        // get polygon world vertices, wcs control points
                        casacore::Quantity cx_wcs, cy_wcs, width_wcs, height_wcs;
                        std::vector<casacore::Quantity> x_wcs, y_wcs;
                        polygon->worldVertices(x_wcs, y_wcs);
                        GetRectangleControlPointsFromVertices(x_wcs, y_wcs, cx_wcs, cy_wcs, width_wcs, height_wcs);
                        _control_points_wcs.push_back(cx_wcs);
                        _control_points_wcs.push_back(cy_wcs);
                        _control_points_wcs.push_back(width_wcs);
                        _control_points_wcs.push_back(height_wcs);
                        _valid = true;
                    }
                    break;
                }
                case casa::AnnotationBase::ROTATED_BOX: {
                    _name = annotation_region->getLabel();
                    _type = CARTA::RegionType::RECTANGLE;
                    // cannot get original rectangle and rotation from AnnRotBox, it is a polygon
                    const casa::AnnRotBox* rotbox = dynamic_cast<const casa::AnnRotBox*>(annotation_region.get());
                    if (rotbox != nullptr) {
                        // store WCRegion
                        std::atomic_store(&_xy_region, rotbox->getRegion2());

                        // parse printed string (known format) to get rotbox input params
                        std::ostringstream rotbox_output;
                        rotbox->print(rotbox_output);
                        casacore::String outputstr(rotbox_output.str()); // "rotbox [[x, y], [x_width, y_width], rotang]"
                        // create comma-delimited string of quantities
                        casacore::String params(outputstr.after("rotbox ")); // remove rotbox
                        params.gsub("[", "");                                // remove [
                        params.gsub("] ", "],");                             // add comma delimiter
                        params.gsub("]", "");                                // remove ]
                        // split string into string vector
                        std::vector<std::string> quantities;
                        SplitString(params, ',', quantities);
                        // convert strings to quantities (Quantum readQuantity)
                        casacore::Quantity cx, cy, xwidth, ywidth, rotang;
                        casacore::readQuantity(cx, quantities[0]);
                        casacore::readQuantity(cy, quantities[1]);
                        casacore::readQuantity(xwidth, quantities[2]);
                        casacore::readQuantity(ywidth, quantities[3]);
                        casacore::readQuantity(rotang, quantities[4]);

                        // make (unrotated) centerbox from parsed quantities; requireImageRegion=false
                        casacore::Vector<casacore::Stokes::StokesTypes> stokes_types = GetStokesTypes();
                        casa::AnnCenterBox cbox = casa::AnnCenterBox(cx, cy, xwidth, ywidth, _coord_sys, _image_shape, stokes_types, false);

                        // get polygon pixel vertices, control points
                        double cx_pix, cy_pix, width_pix, height_pix;
                        std::vector<casacore::Double> x, y;
                        cbox.pixelVertices(x, y);
                        GetRectangleControlPointsFromVertices(x, y, cx_pix, cy_pix, width_pix, height_pix);
                        CARTA::Point point;
                        point.set_x(cx_pix);
                        point.set_y(cy_pix);
                        _control_points.push_back(point);
                        point.set_x(width_pix);
                        point.set_y(height_pix);
                        _control_points.push_back(point);

                        // get polygon world vertices, control points
                        casacore::Quantity cx_wcs, cy_wcs, width_wcs, height_wcs;
                        std::vector<casacore::Quantity> x_wcs, y_wcs;
                        cbox.worldVertices(x_wcs, y_wcs);
                        GetRectangleControlPointsFromVertices(x_wcs, y_wcs, cx_wcs, cy_wcs, width_wcs, height_wcs);
                        _control_points_wcs.push_back(cx_wcs);
                        _control_points_wcs.push_back(cy_wcs);
                        _control_points_wcs.push_back(width_wcs);
                        _control_points_wcs.push_back(height_wcs);

                        // convert rotang to deg
                        rotang.convert("deg");
                        _rotation = rotang.getValue();
                        _valid = true;
                    }
                    break;
                }
                case casa::AnnotationBase::POLYGON: {
                    _name = annotation_region->getLabel();
                    _type = CARTA::RegionType::POLYGON;
                    const casa::AnnPolygon* polygon = dynamic_cast<const casa::AnnPolygon*>(annotation_region.get());
                    if (polygon != nullptr) {
                        // store WCRegion
                        std::atomic_store(&_xy_region, polygon->getRegion2());

                        // get polygon vertices (pixel and world) for control points
                        std::vector<casacore::Double> xpix, ypix;
                        std::vector<casacore::Quantity> xworld, yworld;
                        polygon->pixelVertices(xpix, ypix);
                        polygon->worldVertices(xworld, yworld);
                        for (size_t i = 0; i < xpix.size(); ++i) {
                            CARTA::Point point;
                            point.set_x(xpix[i]);
                            point.set_y(ypix[i]);
                            _control_points.push_back(point);
                            _control_points_wcs.push_back(xworld[i]);
                            _control_points_wcs.push_back(yworld[i]);
                        }
                        _valid = true;
                    }
                    break;
                }
                case casa::AnnotationBase::CIRCLE:
                case casa::AnnotationBase::ELLIPSE: {
                    _name = annotation_region->getLabel();
                    _type = CARTA::RegionType::ELLIPSE;
                    casa::AnnotationBase::Type ann_type = annotation_region->getType();
                    casacore::MDirection center_position;
                    casacore::Quantity bmaj, bmin, position_angle;
                    bool is_ellipse(true);
                    bool have_region_info(false);
                    if (ann_type == casa::AnnotationBase::CIRCLE) {
                        const casa::AnnCircle* circle = dynamic_cast<const casa::AnnCircle*>(annotation_region.get());
                        if (circle != nullptr) {
                            // store WCRegion
                            std::atomic_store(&_xy_region, circle->getRegion2());

                            // get parameters
                            center_position = circle->getCenter();
                            bmaj = circle->getRadius();
                            bmin = bmaj;
                            is_ellipse = false;
                            have_region_info = true;
                        }
                    } else {
                        // if pixels not square, circle is an AnnEllipse
                        const casa::AnnEllipse* ellipse = dynamic_cast<const casa::AnnEllipse*>(annotation_region.get());
                        if (ellipse != nullptr) {
                            // store WCRegion
                            std::atomic_store(&_xy_region, ellipse->getRegion2());

                            // get parameters
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
                        if (_coord_sys.hasDirectionCoordinate()) {
                            casacore::DirectionCoordinate dir_coord = _coord_sys.directionCoordinate();
                            dir_coord.toPixel(pixel_coords, center_position);
                        } else {
                            casacore::Quantum<casacore::Vector<casacore::Double>> angles = center_position.getAngle();
                            casacore::Vector<casacore::Double> world_coords = angles.getValue();
                            world_coords.resize(_coord_sys.nPixelAxes(), true);
                            _coord_sys.toPixel(pixel_coords, world_coords);
                        }
                        CARTA::Point point;
                        point.set_x(pixel_coords[0]);
                        point.set_y(pixel_coords[1]);
                        _control_points.push_back(point);

                        // convert bmaj, bmin to pixel length
                        double bmaj_pixel = AngleToLength(bmaj, 0);
                        double bmin_pixel = AngleToLength(bmin, 1);
                        point.set_x(bmaj_pixel);
                        point.set_y(bmin_pixel);
                        _control_points.push_back(point);

                        // set rotation for ellipse
                        if (is_ellipse) {
                            position_angle.convert("deg");
                            _rotation = position_angle.getValue();
                        }

                        // set control points in Quantities
                        casacore::Quantum<casacore::Vector<casacore::Double>> angle = center_position.getAngle();
                        _control_points_wcs.push_back(casacore::Quantity(angle.getValue()[0], angle.getUnit()));
                        _control_points_wcs.push_back(casacore::Quantity(angle.getValue()[1], angle.getUnit()));
                        _control_points_wcs.push_back(bmaj);
                        _control_points_wcs.push_back(bmin);
                        _valid = true;
                    }
                    break;
                }
                case casa::AnnotationBase::SYMBOL: {
                    const casa::AnnSymbol* point = dynamic_cast<const casa::AnnSymbol*>(annotation_region.get());
                    if (point != nullptr) {
                        // wcs position of point
                        casacore::MDirection position = point->getDirection();
                        // set control points as Quantities
                        casacore::Quantum<casacore::Vector<casacore::Double>> angle = position.getAngle();
                        _control_points_wcs.push_back(casacore::Quantity(angle.getValue()[0], angle.getUnit()));
                        _control_points_wcs.push_back(casacore::Quantity(angle.getValue()[1], angle.getUnit()));

                        // Convert wcs position to pixel coordinates
                        casacore::Vector<casacore::Double> pixel_coords;
                        if (_coord_sys.hasDirectionCoordinate()) {
                            casacore::DirectionCoordinate dir_coord = _coord_sys.directionCoordinate();
                            dir_coord.toPixel(pixel_coords, position);
                        } else {
                            casacore::Quantum<casacore::Vector<casacore::Double>> angles = position.getAngle();
                            casacore::Vector<casacore::Double> world_coords = angles.getValue();
                            world_coords.resize(_coord_sys.nPixelAxes(), true);
                            _coord_sys.toPixel(pixel_coords, world_coords);
                        }

                        // Set CARTA point
                        CARTA::Point point;
                        point.set_x(pixel_coords[0]);
                        point.set_y(pixel_coords[1]);
                        std::vector<CARTA::Point> points;
                        points.push_back(point);
                        // Set other region parameters
                        std::string name = annotation_region->getLabel();
                        CARTA::RegionType type = CARTA::RegionType::POINT;
                        // Set region properties and xy region
                        _valid = UpdateRegionParameters(name, type, points, _rotation);
                    }
                    break;
                }
                case casa::AnnotationBase::POLYLINE: // not supported yet
                case casa::AnnotationBase::ANNULUS:  // not supported yet
                default:
                    break;
            }
        } catch (casacore::AipsError& err) {
            std::cerr << "Import carta region type " << _type << " failed: " << err.getMesg() << std::endl;
        }
    }
    if (_valid) {
        _region_stats = std::unique_ptr<RegionStats>(new RegionStats());
        _region_profiler = std::unique_ptr<RegionProfiler>(new RegionProfiler());
    }
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
    if (_xy_region_changed && _region_stats) {
        _region_stats->ClearStats(); // recalculate everything
        if (type != CARTA::RegionType::POINT) {
            ResetStatsCache(); // Reset stats cache for non-point region
        }
    }

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

void Region::GetRectangleControlPointsFromVertices(
    std::vector<casacore::Double>& x, std::vector<casacore::Double>& y, double& cx, double& cy, double& width, double& height) {
    // Input: pixel vertices x and y
    // Returns: rectangle center point cx and cy, width, and height
    // Point 0 is blc, point 2 is trc
    casacore::Double blc_x = x[0];
    casacore::Double trc_x = x[2];
    casacore::Double blc_y = y[0];
    casacore::Double trc_y = y[2];
    cx = (blc_x + trc_x) / 2.0;
    cy = (blc_y + trc_y) / 2.0;
    width = fabs(trc_x - blc_x);
    height = fabs(trc_y - blc_y);
}

void Region::GetRectangleControlPointsFromVertices(std::vector<casacore::Quantity>& x, std::vector<casacore::Quantity>& y,
    casacore::Quantity& cx, casacore::Quantity& cy, casacore::Quantity& width, casacore::Quantity& height) {
    // Input: world vertices x and y
    // Returns: rectangle center point cx and cy, width, and height as wcs Quantities
    // Point 0 is blc, point 2 is trc
    casacore::Quantity blc_x = x[0];
    casacore::Quantity trc_x = x[2];
    casacore::Quantity blc_y = y[0];
    casacore::Quantity trc_y = y[2];

    cx = (blc_x + trc_x) / 2.0;
    cy = (blc_y + trc_y) / 2.0;
    width = abs(trc_x - blc_x);
    height = abs(trc_y - blc_y);
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
        // set control points as quantities
        _control_points_wcs.clear();
        _control_points_wcs.push_back(world_point(0));
        _control_points_wcs.push_back(world_point(1));
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

        // Set control points (for UNROTATED box) as quantities in wcs
        // Convert pixel coords to world coords
        casacore::Quantum<casacore::Vector<casacore::Double>> x_world, y_world;
        if (!XyPixelsToWorld(x, y, x_world, y_world)) {
            return box_polygon; // nullptr, conversion failed
        }
        // Get blc,trc in wcs
        casacore::String x_unit = x_world.getUnit();
        casacore::String y_unit = y_world.getUnit();
        casacore::Quantity blc_x = casacore::Quantity(x_world.getValue()(0), x_unit);
        casacore::Quantity blc_y = casacore::Quantity(y_world.getValue()(0), y_unit);
        casacore::Quantity trc_x = casacore::Quantity(x_world.getValue()(2), x_unit);
        casacore::Quantity trc_y = casacore::Quantity(y_world.getValue()(2), y_unit);
        // Calculate center point, width, height in world coordinates
        casacore::Quantity cx_wcs = (blc_x + trc_x) / 2.0;
        casacore::Quantity cy_wcs = (blc_y + trc_y) / 2.0;
        casacore::Quantity width_wcs = abs(trc_x - blc_x);
        casacore::Quantity height_wcs = abs(trc_y - blc_y);
        // Save wcs control points
        _control_points_wcs.clear();
        _control_points_wcs.push_back(cx_wcs);
        _control_points_wcs.push_back(cy_wcs);
        _control_points_wcs.push_back(width_wcs);
        _control_points_wcs.push_back(height_wcs);

        if (rotation != 0.0f) {
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

            // Convert pixel coords to world coords vertices
            if (!XyPixelsToWorld(x, y, x_world, y_world)) {
                return box_polygon; // nullptr, conversion failed
            }
        }

        // Create rectangle polygon from vertices
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
            major_axis = casacore::Quantity(bmaj, "pix");
            minor_axis = casacore::Quantity(bmin, "pix");
            rotation_degrees = rotation + 90.0;
        } else {
            major_axis = casacore::Quantity(bmin, "pix");
            minor_axis = casacore::Quantity(bmaj, "pix");
            rotation_degrees = rotation;
        }
        casacore::Quantity theta = casacore::Quantity(rotation_degrees * (M_PI / 180.0f), "rad");

        std::unique_lock<std::mutex> guard(_casacore_region_mutex);
        ellipse = new casacore::WCEllipsoid(
            center_world(0), center_world(1), major_axis, minor_axis, theta, _xy_axes(0), _xy_axes(1), _coord_sys);
        guard.unlock();

        // Set control points as quantities in wcs
        _control_points_wcs.clear();
        _control_points_wcs.push_back(center_world(0));
        _control_points_wcs.push_back(center_world(1));
        // convert npixels to length on given pixel axis
        _control_points_wcs.push_back(_coord_sys.toWorldLength(major_axis.getValue(), 0));
        _control_points_wcs.push_back(_coord_sys.toWorldLength(minor_axis.getValue(), 1));
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

    // Set control points as quantities in wcs
    _control_points_wcs.clear();
    casacore::Vector<casacore::Double> x_world_values = x_world.getValue();
    casacore::Vector<casacore::Double> y_world_values = y_world.getValue();
    casacore::String x_unit = x_world.getUnit();
    casacore::String y_unit = y_world.getUnit();
    for (size_t i = 0; i < x_world_values.size(); ++i) {
        _control_points_wcs.push_back(casacore::Quantity(x_world_values(i), x_unit));
        _control_points_wcs.push_back(casacore::Quantity(y_world_values(i), y_unit));
    }

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
    casa::AnnSymbol* ann_symbol(nullptr); // not a region
    if (!_control_points.empty()) {
        casacore::Vector<casacore::Stokes::StokesTypes> stokes_types = GetStokesTypes();
        bool require_region(false);
        switch (_type) {
            case CARTA::POINT: {
                casacore::Quantity x, y;
                if (pixel_coord) {
                    x = casacore::Quantity(_control_points[0].x(), "pix");
                    y = casacore::Quantity(_control_points[0].y(), "pix");
                } else {
                    x = _control_points_wcs[0];
                    y = _control_points_wcs[1];
                }
                ann_symbol = new casa::AnnSymbol(x, y, _coord_sys, casa::AnnSymbol::POINT, stokes_types);
                break;
            }
            case CARTA::RECTANGLE: {
                casacore::Quantity cx, cy, xwidth, ywidth;
                if (pixel_coord) {
                    cx = casacore::Quantity(_control_points[0].x(), "pix");
                    cy = casacore::Quantity(_control_points[0].y(), "pix");
                    xwidth = casacore::Quantity(_control_points[1].x(), "pix");
                    ywidth = casacore::Quantity(_control_points[1].y(), "pix");
                } else {
                    cx = _control_points_wcs[0];
                    cy = _control_points_wcs[1];
                    xwidth = _control_points_wcs[2];
                    ywidth = _control_points_wcs[3];
                    // adjust width by cosine(declination) for correct import if not linear
                    if (xwidth.isConform("rad")) {
                        xwidth *= cos(cy);
                    }
                }
                if (_rotation == 0.0) {
                    ann_region = new casa::AnnCenterBox(cx, cy, xwidth, ywidth, _coord_sys, _image_shape, stokes_types, require_region);
                } else {
                    casacore::Quantity position_angle(_rotation, "deg");
                    ann_region =
                        new casa::AnnRotBox(cx, cy, xwidth, ywidth, position_angle, _coord_sys, _image_shape, stokes_types, require_region);
                }
                break;
            }
            case CARTA::POLYGON: {
                size_t npoints(_control_points.size());
                casacore::Vector<casacore::Quantity> x_coords(npoints), y_coords(npoints);
                if (pixel_coord) {
                    for (size_t i = 0; i < npoints; ++i) {
                        x_coords(i) = casacore::Quantity(_control_points[i].x(), "pix");
                        y_coords(i) = casacore::Quantity(_control_points[i].y(), "pix");
                    }
                } else {
                    int point_index(0);
                    for (size_t i = 0; i < npoints; ++i) {
                        x_coords(i) = _control_points_wcs[point_index++];
                        y_coords(i) = _control_points_wcs[point_index++];
                    }
                }
                ann_region = new casa::AnnPolygon(x_coords, y_coords, _coord_sys, _image_shape, stokes_types, require_region);
                break;
            }
            case CARTA::ELLIPSE: {
                casacore::Quantity cx, cy, bmaj, bmin;
                if (pixel_coord) {
                    cx = casacore::Quantity(_control_points[0].x(), "pix");
                    cy = casacore::Quantity(_control_points[0].y(), "pix");
                    bmaj = casacore::Quantity(_control_points[1].x(), "pix");
                    bmin = casacore::Quantity(_control_points[1].y(), "pix");
                } else {
                    cx = _control_points_wcs[0];
                    cy = _control_points_wcs[1];
                    bmaj = _control_points_wcs[2];
                    bmin = _control_points_wcs[3];
                }
                casacore::Quantity position_angle(_rotation, "deg");
                ann_region =
                    new casa::AnnEllipse(cx, cy, bmaj, bmin, position_angle, _coord_sys, _image_shape, stokes_types, require_region);
                break;
            }
            default:
                break;
        }
    }

    if (ann_region != nullptr) {
        ann_region->setAnnotationOnly(false);
    }
    casacore::CountedPtr<const casa::AnnotationBase> annotation_region;
    if (ann_symbol != nullptr) {
        annotation_region = casacore::CountedPtr<const casa::AnnotationBase>(ann_symbol);
    } else {
        annotation_region = casacore::CountedPtr<const casa::AnnotationBase>(ann_region);
    }
    return annotation_region;
}

casacore::Vector<casacore::Stokes::StokesTypes> Region::GetStokesTypes() {
    // convert ints to stokes types in vector
    casacore::Vector<casacore::Int> istokes;
    if (_coord_sys.hasPolarizationCoordinate()) {
        casacore::Vector<casacore::Int> istokes = _coord_sys.stokesCoordinate().stokes();
    }
    if (istokes.empty() && (_stokes_axis >= 0)) {
        unsigned int nstokes(_image_shape(_stokes_axis));
        istokes.resize(nstokes);
        for (unsigned int i = 0; i < nstokes; ++i) {
            istokes(i) = i + 1;
        }
    }
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

bool Region::GetBasicStats(int channel, int stokes, BasicStats<float>& stats) {
    if (_region_stats) {
        return _region_stats->GetBasicStats(channel, stokes, stats);
    }
    return false;
}

void Region::SetBasicStats(int channel, int stokes, const BasicStats<float>& stats) {
    if (_region_stats) {
        _region_stats->SetBasicStats(channel, stokes, stats);
    }
}

void Region::CalcBasicStats(int channel, int stokes, const std::vector<float>& data, BasicStats<float>& stats) {
    if (_region_stats) {
        _region_stats->CalcBasicStats(channel, stokes, data, stats);
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

void Region::CalcHistogram(int channel, int stokes, int num_bins, const BasicStats<float>& stats, const std::vector<float>& data,
    CARTA::Histogram& histogram_msg) {
    if (_region_stats) {
        _region_stats->CalcHistogram(channel, stokes, num_bins, stats, data, histogram_msg);
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

// spectral requirements

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

bool Region::IsValidSpectralConfig(const SpectralConfig& config) {
    if (_region_profiler) {
        return _region_profiler->IsValidSpectralConfig(config);
    }
    return false;
}

std::vector<SpectralProfile> Region::GetSpectralProfiles() {
    if (_region_profiler) {
        return _region_profiler->GetSpectralProfiles();
    }
    return std::vector<SpectralProfile>();
}

bool Region::GetSpectralConfig(int config_stokes, SpectralConfig& config) {
    if (_region_profiler) {
        return _region_profiler->GetSpectralConfig(config_stokes, config);
    }
    return false;
}

bool Region::GetSpectralProfileAllStatsSent(int config_stokes) {
    if (_region_profiler) {
        return _region_profiler->GetSpectralProfileAllStatsSent(config_stokes);
    }
    return true;
}

void Region::SetSpectralProfileAllStatsSent(int config_stokes, bool sent) {
    if (_region_profiler) {
        _region_profiler->SetSpectralProfileAllStatsSent(config_stokes, sent);
    }
}

void Region::SetAllSpectralProfilesUnsent() {
    if (_region_profiler) {
        _region_profiler->SetAllSpectralProfilesUnsent();
    }
}

// spectral data

bool Region::GetSpectralProfileData(
    std::map<CARTA::StatsType, std::vector<double>>& spectral_data, casacore::ImageInterface<float>& image) {
    // Return spectral data for all stats in spectral config
    bool have_stats(false);
    if (_region_stats) {
        have_stats = _region_stats->CalcStatsValues(spectral_data, _all_stats, image);
    }
    return have_stats;
}

void Region::FillPointSpectralProfileDataMessage(
    CARTA::SpectralProfileData& profile_message, int config_stokes, std::vector<float>& spectral_data) {
    // Fill SpectralProfileData with unsent spectral_data for point region; assumes one spectral config stats type
    if (IsPoint() && _region_profiler) {
        std::vector<int> unsent_stats;
        if (_region_profiler->GetUnsentStatsForProfile(config_stokes, unsent_stats)) { // true if profile still exists
            if (!unsent_stats.empty()) {
                std::string config_coord(_region_profiler->GetSpectralCoordinate(config_stokes));
                if (!config_coord.empty()) { // not empty if profile still exists
                    auto new_profile = profile_message.add_profiles();
                    new_profile->set_coordinate(config_coord);
                    auto stats_type = static_cast<CARTA::StatsType>(unsent_stats[0]);
                    new_profile->set_stats_type(stats_type);
                    new_profile->set_raw_values_fp32(spectral_data.data(), spectral_data.size() * sizeof(float));
                    if (profile_message.progress() == PROFILE_COMPLETE) {
                        _region_profiler->SetSpectralProfileStatSent(config_stokes, stats_type, true);
                    }
                }
            }
        }
    }
}

void Region::FillSpectralProfileDataMessage(
    CARTA::SpectralProfileData& profile_message, int config_stokes, std::map<CARTA::StatsType, std::vector<double>>& spectral_data) {
    // Fill SpectralProfileData with unsent statistics values (results) for stats in spectral config, using supplied results
    if (_region_profiler) {
        std::vector<int> unsent_stats;
        if (_region_profiler->GetUnsentStatsForProfile(config_stokes, unsent_stats)) { // true if profile still exists
            if (!unsent_stats.empty()) {
                std::string config_coord(_region_profiler->GetSpectralCoordinate(config_stokes));
                if (!config_coord.empty()) { // not empty if profile still exists
                    for (size_t i = 0; i < unsent_stats.size(); ++i) {
                        // one SpectralProfile per stats type
                        auto new_profile = profile_message.add_profiles();
                        new_profile->set_coordinate(config_coord);
                        auto stats_type = static_cast<CARTA::StatsType>(unsent_stats[i]);
                        new_profile->set_stats_type(stats_type);
                        if (spectral_data.find(stats_type) == spectral_data.end()) { // stat not provided
                            double nan_value = std::numeric_limits<double>::quiet_NaN();
                            new_profile->set_raw_values_fp64(&nan_value, sizeof(double));
                        } else {
                            new_profile->set_raw_values_fp64(
                                spectral_data[stats_type].data(), spectral_data[stats_type].size() * sizeof(double));
                        }
                        if (profile_message.progress() == PROFILE_COMPLETE) {
                            _region_profiler->SetSpectralProfileStatSent(config_stokes, stats_type, true);
                        }
                    }
                }
            }
        }
    }
}

void Region::FillNaNSpectralProfileDataMessage(CARTA::SpectralProfileData& profile_message, int config_stokes) {
    // Fill SpectralProfileData with a single NaN value for stats in spectral config; region is fully masked (outside image or NaNs)
    if (_region_profiler) {
        std::vector<int> unsent_stats;
        if (_region_profiler->GetUnsentStatsForProfile(config_stokes, unsent_stats)) { // true if profile still exists
            if (!unsent_stats.empty()) {
                std::string config_coord(_region_profiler->GetSpectralCoordinate(config_stokes));
                if (!config_coord.empty()) { // not empty if profile still exists
                    for (size_t i = 0; i < unsent_stats.size(); ++i) {
                        // one SpectralProfile per stats type
                        auto new_profile = profile_message.add_profiles();
                        new_profile->set_coordinate(config_coord);
                        auto stat_type = static_cast<CARTA::StatsType>(unsent_stats[i]);
                        new_profile->set_stats_type(stat_type);
                        double nan_value = std::numeric_limits<double>::quiet_NaN();
                        new_profile->set_raw_values_fp64(&nan_value, sizeof(double));
                        _region_profiler->SetSpectralProfileStatSent(config_stokes, stat_type, true);
                    }
                }
            }
        }
    }
}

void Region::InitSpectralData(
    int profile_stokes, size_t profile_size, std::map<CARTA::StatsType, std::vector<double>>& spectral_data, size_t& channel_start) {
    // Initialize spectral data map for all stats
    channel_start = std::numeric_limits<size_t>::max();
    for (size_t i = 0; i < _all_stats.size(); ++i) {
        auto stats_type = static_cast<CARTA::StatsType>(_all_stats[i]);
        std::vector<double> buffer;
        size_t tmp_channel_start;
        if (GetStatsCache(profile_stokes, profile_size, stats_type, buffer, tmp_channel_start)) {
            // Stats cache is available, reuse it.
            spectral_data.emplace(stats_type, buffer);
        } else {
            // Initialize spectral data map for the stats to NaN
            std::vector<double> init_spectral_data(profile_size, std::numeric_limits<double>::quiet_NaN());
            spectral_data.emplace(stats_type, init_spectral_data);
            tmp_channel_start = 0;
        }
        // Use the minimum of channel_start for all stats types (to be conservative),
        // which is used to determine the start channel of spectral profile calculations.
        if (tmp_channel_start < channel_start) {
            channel_start = tmp_channel_start;
        }
    }
}

// Region connection state

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

void Region::SetStatsCache(int profile_stokes, std::map<CARTA::StatsType, std::vector<double>>& stats_data, size_t channel_end) {
    std::unique_lock<std::mutex> lock(_stats_cache_mutex);
    for (auto& stats_data_elem : stats_data) {
        auto& stats_type = stats_data_elem.first;
        std::vector<double>& stats_values = stats_data_elem.second;
        // Set stats cache
        StatsCacheData& stats_cache_data = _stats_cache[profile_stokes][stats_type];
        stats_cache_data.stats_values = std::move(stats_values);
        stats_cache_data.channel_end = channel_end;
    }
}

bool Region::GetStatsCache(
    int profile_stokes, size_t profile_size, CARTA::StatsType stats_type, std::vector<double>& stats_data, size_t& channel_start) {
    bool cache_ok(false);
    channel_start = std::numeric_limits<size_t>::max();
    std::unique_lock<std::mutex> lock(_stats_cache_mutex);
    if (_stats_cache.count(profile_stokes)) {
        auto& stats_cache_stoke = _stats_cache[profile_stokes];
        if (stats_cache_stoke.count(stats_type)) {
            StatsCacheData& stats_cache_stoke_type = stats_cache_stoke[stats_type];
            stats_data = stats_cache_stoke_type.stats_values;
            channel_start = stats_cache_stoke_type.channel_end;
            // Check does stats cache fit requirements
            if ((channel_start > 0 && channel_start <= profile_size) && (stats_data.size() == profile_size)) {
                cache_ok = true;
            }
        }
    }
    return cache_ok;
}

void Region::ResetStatsCache() {
    std::unique_lock<std::mutex> lock(_stats_cache_mutex);
    _stats_cache.clear();
}
