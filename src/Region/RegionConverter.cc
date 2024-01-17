/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# RegionConverter.cc: implementation of class for converting a region

#include "RegionConverter.h"

#include <casacore/casa/Quanta/QLogical.h>
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

#include "Logger/Logger.h"
#include "Util/Image.h"
#include "Util/Message.h"

using namespace carta;

RegionConverter::RegionConverter(const RegionState& state, std::shared_ptr<casacore::CoordinateSystem> csys)
    : _region_state(state), _reference_coord_sys(csys), _reference_region_set(false) {}

// ****************************************************************************
// Apply region to reference image in world coordinates (WCRegion) and save wcs control points

bool RegionConverter::ReferenceRegionValid() {
    return _reference_region_set && bool(_reference_region);
}

void RegionConverter::SetReferenceWCRegion() {
    // Create WCRegion (world coordinate region) in the reference image according to type using wcs control points.
    // Sets _reference_region (including to nullptr if fails) and _reference_region_set (attempted).
    // Supports closed (not line-type) annotation regions, for conversion to matched image LCRegion then Record for export.
    // Rotated box is rotated, do not use for Record!
    casacore::WCRegion* region(nullptr);
    std::vector<casacore::Quantity> world_points; // for converting one point at a time
    casacore::IPosition pixel_axes(2, 0, 1);      // first and second axes only
    casacore::Vector<casacore::Int> abs_rel;
    auto type(_region_state.type);

    try {
        switch (type) {
            case CARTA::POINT:
            case CARTA::ANNPOINT: {
                // Convert one point for wcs control points
                if (CartaPointToWorld(_region_state.control_points[0], _wcs_control_points)) {
                    // WCBox blc and trc are same point
                    region = new casacore::WCBox(_wcs_control_points, _wcs_control_points, pixel_axes, *_reference_coord_sys, abs_rel);
                }
                break;
            }
            case CARTA::RECTANGLE:    // 4 corners
            case CARTA::POLYGON:      // vertices
            case CARTA::ANNRECTANGLE: // 4 corners
            case CARTA::ANNPOLYGON:   // vertices
            case CARTA::ANNTEXT: {    // 4 corners of text box
                // Use corners/vertices for wcs control points: x0, y0, x1, y1, etc.
                if (type == CARTA::RECTANGLE || type == CARTA::ANNRECTANGLE || type == CARTA::ANNTEXT) {
                    if (!RectangleControlPointsToWorld(_wcs_control_points)) {
                        _wcs_control_points.clear();
                    }
                } else {
                    for (auto& point : _region_state.control_points) {
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
                    // Convert from Vector<Quantum> (wcs control points) to Quantum<Vector> for WCPolygon
                    casacore::Quantum<casacore::Vector<casacore::Double>> qx, qy;
                    // Separate x and y
                    std::vector<double> x, y;
                    for (size_t i = 0; i < _wcs_control_points.size(); i += 2) {
                        x.push_back(_wcs_control_points[i].getValue());
                        y.push_back(_wcs_control_points[i + 1].getValue());
                    }
                    casacore::Vector<casacore::Double> x_v(x), y_v(y);
                    casacore::Vector<casacore::String> world_units = _reference_coord_sys->worldAxisUnits();
                    qx = x_v;                   // set values
                    qx.setUnit(world_units(0)); // set unit
                    qy = y_v;                   // set values
                    qy.setUnit(world_units(1)); // set unit

                    region = new casacore::WCPolygon(qx, qy, pixel_axes, *_reference_coord_sys);
                }
                break;
            }
            case CARTA::ELLIPSE:      // [(cx, cy), (bmaj, bmin)]
            case CARTA::ANNELLIPSE:   // [(cx, cy), (bmaj, bmin)]
            case CARTA::ANNCOMPASS: { // [(cx, cy), (length, length)}
                float ellipse_rotation;
                if (EllipseControlPointsToWorld(_wcs_control_points, ellipse_rotation)) {
                    // wcs control points order: xcenter, ycenter, major axis, minor axis
                    casacore::Quantity theta(ellipse_rotation, "deg");
                    theta.convert("rad");
                    region = new casacore::WCEllipsoid(_wcs_control_points[0], _wcs_control_points[1], _wcs_control_points[2],
                        _wcs_control_points[3], theta, 0, 1, *_reference_coord_sys);
                }
                break;
            }
            default: // no WCRegion for line-type regions
                break;
        }
    } catch (casacore::AipsError& err) { // region failed
        spdlog::error("region type {} failed: {}", type, err.getMesg());
    }

    std::shared_ptr<casacore::WCRegion> shared_region = std::shared_ptr<casacore::WCRegion>(region);
    std::atomic_store(&_reference_region, shared_region);

    // Flag indicates that attempt was made, to avoid repeated attempts
    _reference_region_set = true;
}

bool RegionConverter::RectangleControlPointsToWorld(std::vector<casacore::Quantity>& world_corners) {
    // Convert CARTA rectangle points (cx, cy), (width, height) to corners in world coordinates (reference image)
    // Get 4 corner points in pixel coordinates from control points, applying rotation
    casacore::Vector<casacore::Double> x, y;
    if (!_region_state.GetRectangleCorners(x, y)) {
        return false;
    }

    // Convert corners to wcs in one call for efficiency, rather than one point at a time
    size_t num_points(x.size()), num_axes(_reference_coord_sys->nPixelAxes());
    casacore::Matrix<casacore::Double> pixel_coords(num_axes, num_points);
    casacore::Matrix<casacore::Double> world_coords(num_axes, num_points);
    pixel_coords = 0.0;
    pixel_coords.row(0) = x;
    pixel_coords.row(1) = y;
    casacore::Vector<casacore::Bool> failures;
    if (!_reference_coord_sys->toWorldMany(world_coords, pixel_coords, failures)) {
        return false;
    }

    // Save x and y values as Quantities
    casacore::Vector<casacore::String> world_units = _reference_coord_sys->worldAxisUnits();
    casacore::Vector<double> x_wcs = world_coords.row(0);
    casacore::Vector<double> y_wcs = world_coords.row(1);
    // reference points: corners (x0, y0, x1, y1, x2, y2, x3, y3) in world coordinates
    world_corners.resize(num_points * 2);
    for (int i = 0; i < num_points; ++i) {
        world_corners[i * 2] = casacore::Quantity(x_wcs(i), world_units(0));
        world_corners[(i * 2) + 1] = casacore::Quantity(y_wcs(i), world_units(1));
    }
    return true;
}

bool RegionConverter::EllipseControlPointsToWorld(std::vector<casacore::Quantity>& wcs_points, float& ellipse_rotation) {
    // Convert CARTA ellipse points (cx, cy), (bmaj, bmin) to world coordinates, adjust rotation
    auto pixel_points = _region_state.control_points;
    ellipse_rotation = _region_state.rotation;

    // Convert center and store in wcs points
    std::vector<casacore::Quantity> center_world;
    if (!CartaPointToWorld(pixel_points[0], center_world)) {
        return false;
    }
    wcs_points = center_world;

    // Convert bmaj, bmin from pixel length to world length
    float bmaj(pixel_points[1].x()), bmin(pixel_points[1].y());
    casacore::Quantity bmaj_world = _reference_coord_sys->toWorldLength(bmaj, 0);
    casacore::Quantity bmin_world = _reference_coord_sys->toWorldLength(bmin, 1);

    // Check if bmaj/bmin units conform (false for PV image, in arcsec and Hz)
    if (!bmaj_world.isConform(bmin_world.getUnit())) {
        return false;
    }

    // bmaj > bmin (world coords) required for WCEllipsoid; adjust rotation angle
    if (bmaj_world > bmin_world) {
        // carta rotation is from y-axis, ellipse rotation is from x-axis
        ellipse_rotation += 90.0;
    } else {
        // swapping takes care of 90 deg adjustment
        std::swap(bmaj_world, bmin_world);
    }

    wcs_points.push_back(bmaj_world);
    wcs_points.push_back(bmin_world);
    return true;
}

bool RegionConverter::CartaPointToWorld(const CARTA::Point& point, std::vector<casacore::Quantity>& world_point) {
    // Converts a CARTA point(x, y) in pixel coordinates to a Quantity vector [x, y] in world coordinates.
    // Returns whether conversion was successful

    // Vectors must be same number of axes as in coord system for conversion:
    int naxes(_reference_coord_sys->nPixelAxes());
    casacore::Vector<casacore::Double> pixel_values(naxes), world_values(naxes);
    pixel_values = 0.0; // set "extra" axes to 0, not needed
    pixel_values(0) = point.x();
    pixel_values(1) = point.y();

    // convert pixel vector to world vector
    if (!_reference_coord_sys->toWorld(world_values, pixel_values)) {
        return false;
    }

    // Set Quantities from world values and units
    casacore::Vector<casacore::String> world_units = _reference_coord_sys->worldAxisUnits();
    world_point.clear();
    world_point.push_back(casacore::Quantity(world_values(0), world_units(0)));
    world_point.push_back(casacore::Quantity(world_values(1), world_units(1)));
    return true;
}

// *************************************************************************
// Convert region to any image

std::shared_ptr<casacore::LCRegion> RegionConverter::GetCachedLCRegion(int file_id, bool use_approx_polygon) {
    // Return cached region applied to image with file_id, if cached.
    std::lock_guard<std::mutex> guard(_region_mutex);
    if (_converted_regions.find(file_id) != _converted_regions.end()) {
        return _converted_regions.at(file_id);
    } else if (use_approx_polygon && _polygon_regions.find(file_id) != _polygon_regions.end()) {
        // Return cached polygon-approximated region applied to image with file_id
        return _polygon_regions.at(file_id);
    }

    return std::shared_ptr<casacore::LCRegion>();
}

std::shared_ptr<casacore::LCRegion> RegionConverter::GetImageRegion(int file_id, std::shared_ptr<casacore::CoordinateSystem> output_csys,
    const casacore::IPosition& output_shape, const StokesSource& stokes_source, bool report_error) {
    // Apply region to non-reference image, possibly as an approximate polygon to avoid distortion.
    std::shared_ptr<casacore::LCRegion> lc_region;

    // Analytic, closed regions only
    if (_region_state.IsLineType() || _region_state.IsAnnotation()) {
        return lc_region;
    }

    if (stokes_source.IsOriginalImage()) {
        // The cache of converted LCRegions is only for the original image (not computed stokes image). In order to avoid the ambiguity
        lc_region = GetCachedLCRegion(file_id);
    }

    if (!lc_region) {
        // Create converted LCRegion and cache it
        if (!UseApproximatePolygon(output_csys)) {
            // Do direct region conversion from reference WCRegion (no distortion detected)
            lc_region = GetConvertedLCRegion(file_id, output_csys, output_shape, stokes_source, report_error);
            if (lc_region) {
                spdlog::debug("Using direct region conversion for matched image");
            }
        }

        if (!lc_region) {
            // Approximate region as polygon points then convert the points.
            lc_region = GetAppliedPolygonRegion(file_id, output_csys, output_shape);

            if (lc_region) {
                spdlog::debug("Using polygon approximation for region in matched image");
                if (stokes_source.IsOriginalImage()) {
                    // Cache converted polygon, only for the original image (not computed stokes).
                    std::lock_guard<std::mutex> guard(_region_mutex);
                    _polygon_regions[file_id] = lc_region;
                }
            }
        }
    }

    return lc_region;
}

std::shared_ptr<casacore::LCRegion> RegionConverter::GetConvertedLCRegion(int file_id,
    std::shared_ptr<casacore::CoordinateSystem> output_csys, const casacore::IPosition& output_shape, const StokesSource& stokes_source,
    bool report_error) {
    // Convert reference WCRegion to LCRegion in output coord_sys and shape, and cache converted region.
    // Check cache before calling this else will needlessly create a new LCRegion and cache it.
    std::shared_ptr<casacore::LCRegion> lc_region;

    try {
        // Convert reference WCRegion to LCRegion using output csys and shape
        if (!_reference_region_set) {
            SetReferenceWCRegion();
        }

        if (ReferenceRegionValid()) {
            std::shared_ptr<const casacore::WCRegion> reference_region = std::atomic_load(&_reference_region);
            lc_region.reset(reference_region->toLCRegion(*output_csys.get(), output_shape));
        }
    } catch (const casacore::AipsError& err) {
        if (report_error) {
            spdlog::error("Error converting region type {} to file {}: {}", _region_state.type, file_id, err.getMesg());
        }
    }

    if (lc_region && stokes_source.IsOriginalImage()) {
        // Cache the lattice coordinate region only for the original image (not computed stokes image).
        std::lock_guard<std::mutex> guard(_region_mutex);
        _converted_regions[file_id] = lc_region;
    }

    return lc_region;
}

// *************************************************************************
// Region as polygon to avoid distortion in matched image

bool RegionConverter::UseApproximatePolygon(std::shared_ptr<casacore::CoordinateSystem> output_csys) {
    // Determine whether to convert region directly, or approximate it as a polygon in the output image.
    // Closed region types: rectangle, ellipse, polygon.
    // Check ellipse and rectangle distortion; always use polygon for polygon regions.
    CARTA::RegionType region_type = _region_state.type;
    if ((region_type != CARTA::RegionType::ELLIPSE) && (region_type != CARTA::RegionType::RECTANGLE)) {
        return true;
    }

    // Ratio of vector lengths in reference image region
    double ref_length_ratio;
    double x_length(_region_state.control_points[1].x()), y_length(_region_state.control_points[1].y());
    if (region_type == CARTA::RegionType::ELLIPSE) {
        ref_length_ratio = x_length / y_length;
    } else {
        ref_length_ratio = y_length / x_length;
    }

    // Make vector of endpoints and center, to check lengths against reference image lengths
    std::vector<CARTA::Point> points; // [p0, p1, p2, p3, center]
    if (region_type == CARTA::RegionType::ELLIPSE) {
        // Make "polygon" with only 4 points
        points = GetApproximateEllipsePoints(4);
    } else {
        // Get midpoints of 4 sides of rectangle
        points = GetRectangleMidpoints();
    }
    points.push_back(_region_state.control_points[0]);

    // Convert reference pixel points to output pixel points, then check vector length ratio and dot product
    casacore::Vector<casacore::Double> x, y;
    if (PointsToImagePixels(points, output_csys, x, y)) {
        // vector0 is (center, p0), vector1 is (center, p1)
        auto v0_delta_x = x[0] - x[4];
        auto v0_delta_y = y[0] - y[4];
        auto v1_delta_x = x[1] - x[4];
        auto v1_delta_y = y[1] - y[4];

        // Compare reference length ratio to converted length ratio
        // Ratio of vector lengths in converted region
        auto v0_length = sqrt((v0_delta_x * v0_delta_x) + (v0_delta_y * v0_delta_y));
        auto v1_length = sqrt((v1_delta_x * v1_delta_x) + (v1_delta_y * v1_delta_y));
        double converted_length_ratio = v1_length / v0_length;
        double length_ratio_difference = fabs(ref_length_ratio - converted_length_ratio);
        // spdlog::debug("{} distortion check: length ratio difference={:.3e}", length_ratio_difference);
        if (length_ratio_difference > 1e-4) {
            // Failed ratio check, use polygon
            return true;
        }

        // Passed ratio check; check dot product of converted region
        double converted_dot_product = (v0_delta_x * v1_delta_x) + (v0_delta_y * v1_delta_y);
        // spdlog::debug("{} distortion check: dot product={:.3e}", converted_dot_product);
        if (fabs(converted_dot_product) > 1e-2) {
            // failed dot product test, use polygon
            return true;
        }
    } else {
        spdlog::error("Error converting region points to matched image.");
        return true;
    }

    return false;
}

std::vector<CARTA::Point> RegionConverter::GetRectangleMidpoints() {
    // Return midpoints of 4 sides of rectangle
    // Find corners with rotation: blc, brc, trc, tlc
    std::vector<CARTA::Point> midpoints;
    casacore::Vector<casacore::Double> x, y;
    if (_region_state.GetRectangleCorners(x, y)) {
        // start with right side brc, trc
        midpoints.push_back(Message::Point((x[1] + x[2]) / 2.0, (y[1] + y[2]) / 2.0));
        midpoints.push_back(Message::Point((x[2] + x[3]) / 2.0, (y[2] + y[3]) / 2.0));
        midpoints.push_back(Message::Point((x[3] + x[0]) / 2.0, (y[3] + y[0]) / 2.0));
        midpoints.push_back(Message::Point((x[0] + x[1]) / 2.0, (y[0] + y[1]) / 2.0));
    }
    return midpoints;
}

std::shared_ptr<casacore::LCRegion> RegionConverter::GetAppliedPolygonRegion(
    int file_id, std::shared_ptr<casacore::CoordinateSystem> output_csys, const casacore::IPosition& output_shape) {
    // Approximate region as polygon pixel vertices, and convert to given csys
    std::shared_ptr<casacore::LCRegion> lc_region;

    bool is_point(_region_state.IsPoint());
    size_t nvertices(is_point ? 1 : DEFAULT_VERTEX_COUNT);

    // Set reference region as points along polygon segments
    auto polygon_points = GetReferencePolygonPoints(nvertices);
    if (polygon_points.empty()) {
        return lc_region;
    }

    // Convert polygon points to x and y pixel coords in matched image
    casacore::Vector<casacore::Double> x, y;
    if (polygon_points.size() == 1) {
        // Point and ellipse have one vector for all points
        if (!PointsToImagePixels(polygon_points[0], output_csys, x, y)) {
            spdlog::error("Error approximating region as polygon in matched image.");
            return lc_region;
        }
        if (!is_point) {
            // if ~horizontal then remove intermediate points to fix distortion
            RemoveHorizontalPolygonPoints(x, y);
        }
    } else {
        // Rectangle and polygon have one vector for each segment of original rectangle/polygon
        for (auto& segment : polygon_points) {
            casacore::Vector<casacore::Double> segment_x, segment_y;
            if (!PointsToImagePixels(segment, output_csys, segment_x, segment_y)) {
                spdlog::error("Error approximating region as polygon in matched image.");
                return lc_region;
            }

            // if ~horizontal then remove intermediate points to fix distortion
            RemoveHorizontalPolygonPoints(segment_x, segment_y);

            auto old_size = x.size();
            x.resize(old_size + segment_x.size(), true);
            y.resize(old_size + segment_y.size(), true);

            // Append selected segment points
            for (auto i = 0; i < segment_x.size(); ++i) {
                x[old_size + i] = segment_x[i];
                y[old_size + i] = segment_y[i];
            }
        }
    }

    // Use converted pixel points to create LCRegion (LCBox for point, else LCPolygon)
    try {
        if (is_point) {
            // Point is not a polygon (needs at least 3 points), use LCBox instead with blc, trc = point
            size_t ndim(output_shape.size());
            casacore::Vector<casacore::Float> blc(ndim, 0.0), trc(ndim);
            blc(0) = x(0);
            blc(1) = y(0);
            trc(0) = x(0);
            trc(1) = y(0);

            for (size_t i = 2; i < ndim; ++i) {
                trc(i) = output_shape(i) - 1;
            }

            lc_region.reset(new casacore::LCBox(blc, trc, output_shape));
        } else {
            // Need 2D shape
            casacore::IPosition keep_axes(2, 0, 1);
            casacore::IPosition region_shape(output_shape.keepAxes(keep_axes));
            lc_region.reset(new casacore::LCPolygon(x, y, region_shape));
        }
    } catch (const casacore::AipsError& err) {
        spdlog::error("Cannot apply region type {} to file {}: {}", _region_state.type, file_id, err.getMesg());
    }

    return lc_region;
}

std::vector<std::vector<CARTA::Point>> RegionConverter::GetReferencePolygonPoints(int num_vertices) {
    // Approximate reference region as polygon with input number of vertices.
    // Returns points for supported region types.
    std::vector<std::vector<CARTA::Point>> points;

    switch (_region_state.type) {
        case CARTA::POINT: {
            points.push_back(_region_state.control_points);
        }
        case CARTA::RECTANGLE:
        case CARTA::POLYGON: {
            return GetApproximatePolygonPoints(num_vertices);
        }
        case CARTA::ELLIPSE: {
            points.push_back(GetApproximateEllipsePoints(num_vertices));
        }
        default:
            return points;
    }
    return points;
}

std::vector<std::vector<CARTA::Point>> RegionConverter::GetApproximatePolygonPoints(int num_vertices) {
    // Approximate RECTANGLE or POLYGON region as polygon with num_vertices.
    // Returns vector of points for each segment of polygon, or empty vector for other region types.
    std::vector<std::vector<CARTA::Point>> polygon_points;
    std::vector<CARTA::Point> region_vertices;
    CARTA::RegionType region_type(_region_state.type);

    // Rectangle corners or polygon points as polygon vertices for segments.
    if (region_type == CARTA::RegionType::RECTANGLE) {
        casacore::Vector<casacore::Double> x, y;
        _region_state.GetRectangleCorners(x, y);

        for (size_t i = 0; i < x.size(); ++i) {
            region_vertices.push_back(Message::Point(x(i), y(i)));
        }
    } else if (region_type == CARTA::RegionType::POLYGON) {
        region_vertices = _region_state.control_points;
    } else {
        spdlog::error("Error approximating region as polygon: type {} not supported", _region_state.type);
        return polygon_points;
    }

    // Close polygon
    CARTA::Point first_point(region_vertices[0]);
    region_vertices.push_back(first_point);

    double total_length = GetTotalSegmentLength(region_vertices);
    double target_segment_length = total_length / num_vertices;

    // Divide each polygon segment into target number of segments with target length
    for (size_t i = 1; i < region_vertices.size(); ++i) {
        // Handle segment from point[i-1] to point[i]
        std::vector<CARTA::Point> segment_points;

        auto delta_x = region_vertices[i].x() - region_vertices[i - 1].x();
        auto delta_y = region_vertices[i].y() - region_vertices[i - 1].y();
        auto segment_length = sqrt((delta_x * delta_x) + (delta_y * delta_y));
        auto dir_x = delta_x / segment_length;
        auto dir_y = delta_y / segment_length;
        auto target_nsegment = round(segment_length / target_segment_length);
        auto target_length = segment_length / target_nsegment;

        auto first_segment_point(region_vertices[i - 1]);
        segment_points.push_back(first_segment_point);

        auto first_x(first_segment_point.x());
        auto first_y(first_segment_point.y());

        for (size_t j = 1; j < target_nsegment; ++j) {
            auto length_from_first = j * target_length;
            auto x_offset = dir_x * length_from_first;
            auto y_offset = dir_y * length_from_first;
            segment_points.push_back(Message::Point(first_x + x_offset, first_y + y_offset));
        }

        polygon_points.push_back(segment_points);
    }

    return polygon_points;
}

std::vector<CARTA::Point> RegionConverter::GetApproximateEllipsePoints(int num_vertices) {
    // Approximate ELLIPSE region as polygon with num_vertices, return points
    std::vector<CARTA::Point> polygon_points;

    auto cx = _region_state.control_points[0].x();
    auto cy = _region_state.control_points[0].y();
    auto bmaj = _region_state.control_points[1].x();
    auto bmin = _region_state.control_points[1].y();

    auto delta_theta = 2.0 * M_PI / num_vertices;
    auto rotation = _region_state.rotation * M_PI / 180.0;
    auto cos_rotation = cos(rotation);
    auto sin_rotation = sin(rotation);

    for (int i = 0; i < num_vertices; ++i) {
        auto theta = i * delta_theta;
        auto rot_bmin = bmin * cos(theta);
        auto rot_bmaj = bmaj * sin(theta);

        auto x_offset = (cos_rotation * rot_bmin) - (sin_rotation * rot_bmaj);
        auto y_offset = (sin_rotation * rot_bmin) + (cos_rotation * rot_bmaj);

        polygon_points.push_back(Message::Point(cx + x_offset, cy + y_offset));
    }

    return polygon_points;
}

double RegionConverter::GetTotalSegmentLength(std::vector<CARTA::Point>& points) {
    // Accumulate length of each point-to-point segment; returns total length.
    double total_length(0.0);

    for (size_t i = 1; i < points.size(); ++i) {
        auto delta_x = points[i].x() - points[i - 1].x();
        auto delta_y = points[i].y() - points[i - 1].y();
        total_length += sqrt((delta_x * delta_x) + (delta_y * delta_y));
    }

    return total_length;
}

void RegionConverter::RemoveHorizontalPolygonPoints(casacore::Vector<casacore::Double>& x, casacore::Vector<casacore::Double>& y) {
    // When polygon points have close y-points (horizontal segment), the x-range is masked only to the next point.
    // Remove points not near integral pixel.
    std::vector<casacore::Double> keep_x, keep_y;
    size_t npoints(x.size());

    for (int i = 0; i < npoints - 2; ++i) {
        if (i == 0) {
            // always include first point of segment
            keep_x.push_back(x[i]);
            keep_y.push_back(y[i]);
            continue;
        }

        float this_y = y[i];
        float next_y = y[i + 1];
        if (!ValuesNear(this_y, next_y)) {
            // Line connecting points not ~horizontal - keep point
            keep_x.push_back(x[i]);
            keep_y.push_back(y[i]);
            continue;
        }

        // Line connecting points ~horizontal - keep point nearest integral pixel
        int pixel_y = static_cast<int>(this_y);

        if (!ValuesNear(this_y, float(pixel_y))) {
            // Skip point not near pixel
            continue;
        }

        if ((static_cast<int>(next_y) == pixel_y) && ((this_y - pixel_y) > (next_y - pixel_y))) {
            // Skip point if next point nearer to pixel
            continue;
        }

        keep_x.push_back(x[i]);
        keep_y.push_back(y[i]);
    }

    if (keep_x.size() < npoints) {
        // Set to new vector with points removed
        x = casacore::Vector<casacore::Double>(keep_x);
        y = casacore::Vector<casacore::Double>(keep_y);
    }
}

bool RegionConverter::ValuesNear(float val1, float val2) {
    // near and nearAbs in casacore Math
    return val1 == 0 || val2 == 0 ? casacore::nearAbs(val1, val2) : casacore::near(val1, val2);
}

// ***************************************************************
// Apply region to any image and return LCRegion Record for export

casacore::TableRecord RegionConverter::GetImageRegionRecord(
    int file_id, std::shared_ptr<casacore::CoordinateSystem> output_csys, const casacore::IPosition& output_shape) {
    // Return Record describing Region applied to output image in pixel coordinates
    casacore::TableRecord record;

    // No LCRegion for lines. LCRegion for rectangles is a rotated polygon; Record should be unrotated box.
    if (!_region_state.IsLineType() && !_region_state.IsRotbox()) {
        // Get record from converted LCRegion, for enclosed regions only.
        // Check converted regions cache (but not polygon regions cache)
        std::shared_ptr<casacore::LCRegion> lc_region = GetCachedLCRegion(file_id, false);

        // Convert reference region to output image
        if (!lc_region) {
            lc_region = GetConvertedLCRegion(file_id, output_csys, output_shape);
        }

        // Get LCRegion definition as Record
        if (lc_region) {
            std::cerr << "Get record from converted LCRegion" << std::endl;
            record = lc_region->toRecord("region");
            if (record.isDefined("region")) {
                record = record.asRecord("region");
            }
        }
    }

    if (record.empty()) {
        // LCRegion failed, is outside the image or a rotated rectangle.
        // Manually convert control points and put in Record.
        std::cerr << "Get record from converted control points" << std::endl;
        record = GetRegionPointsRecord(output_csys, output_shape);
    }
    std::cerr << "RegionConverter returning record=" << record << std::endl;
    return record;
}

casacore::TableRecord RegionConverter::GetRegionPointsRecord(
    std::shared_ptr<casacore::CoordinateSystem> output_csys, const casacore::IPosition& output_shape) {
    // Convert control points to output coord sys if needed, and return completed record.
    // Used when LCRegion::toRecord() fails, usually outside image.
    casacore::TableRecord record;
    if (!_reference_region_set) {
        SetReferenceWCRegion(); // set wcs control points
    }

    switch (_region_state.type) {
        case CARTA::RegionType::POINT:
        case CARTA::RegionType::ANNPOINT: {
            record = GetPointRecord(output_csys, output_shape);
            break;
        }
        case CARTA::RegionType::LINE:
        case CARTA::RegionType::POLYLINE:
        case CARTA::RegionType::ANNLINE:
        case CARTA::RegionType::ANNPOLYLINE:
        case CARTA::RegionType::ANNVECTOR:
        case CARTA::RegionType::ANNRULER: {
            record = GetLineRecord(output_csys);
            break;
        }
        case CARTA::RegionType::RECTANGLE:
        case CARTA::RegionType::POLYGON:
        case CARTA::RegionType::ANNRECTANGLE:
        case CARTA::RegionType::ANNPOLYGON:
        case CARTA::RegionType::ANNTEXT: {
            // Rectangle types are LCPolygon with 4 (unrotated) corners.
            record = (_region_state.IsRotbox() ? GetRotboxRecord(output_csys) : GetPolygonRecord(output_csys));
            break;
        }
        case CARTA::RegionType::ELLIPSE:
        case CARTA::RegionType::ANNELLIPSE:
        case CARTA::RegionType::ANNCOMPASS: {
            record = GetEllipseRecord(output_csys);
            break;
        }
        default:
            break;
    }
    return record;
}

casacore::TableRecord RegionConverter::GetPointRecord(
    std::shared_ptr<casacore::CoordinateSystem> output_csys, const casacore::IPosition& output_shape) {
    // Convert wcs points to output image in format of LCBox::toRecord()
    casacore::TableRecord record;
    try {
        // wcs control points is single point (x, y)
        casacore::Vector<casacore::Double> pixel_point;
        if (WorldPointToImagePixels(_wcs_control_points, output_csys, pixel_point)) {
            auto ndim = output_shape.size();
            casacore::Vector<casacore::Float> blc(ndim), trc(ndim);
            blc = 0.0;
            blc(0) = pixel_point(0);
            blc(1) = pixel_point(1);
            trc(0) = pixel_point(0);
            trc(1) = pixel_point(1);

            for (size_t i = 2; i < ndim; ++i) {
                trc(i) = output_shape(i) - 1;
            }

            record.define("name", "LCBox");
            record.define("blc", blc);
            record.define("trc", trc);
        } else {
            spdlog::error("Error converting point to image.");
        }
    } catch (const casacore::AipsError& err) {
        spdlog::error("Error converting point to image: {}", err.getMesg());
    }

    return record;
}

casacore::TableRecord RegionConverter::GetLineRecord(std::shared_ptr<casacore::CoordinateSystem> image_csys) {
    // Convert control points for line-type region to output image pixels in format of LCPolygon::toRecord()
    casacore::TableRecord record;
    casacore::Vector<casacore::Double> x, y;
    if (PointsToImagePixels(_region_state.control_points, image_csys, x, y)) {
        record.define("name", _region_state.GetLineRegionName());
        record.define("x", x);
        record.define("y", y);
    }
    return record;
}

casacore::TableRecord RegionConverter::GetPolygonRecord(std::shared_ptr<casacore::CoordinateSystem> output_csys) {
    // Convert wcs points to output image in format of LCPolygon::toRecord()
    // This is for POLYGON or RECTANGLE (points are four corners of box)
    casacore::TableRecord record;
    auto type = _region_state.type;

    try {
        size_t npoints(_wcs_control_points.size() / 2);
        casacore::Vector<casacore::Float> x(npoints), y(npoints); // Record fields

        // Convert each wcs control point to pixel coords in output csys
        for (size_t i = 0; i < _wcs_control_points.size(); i += 2) {
            std::vector<casacore::Quantity> world_point(2);
            world_point[0] = _wcs_control_points[i];
            world_point[1] = _wcs_control_points[i + 1];
            casacore::Vector<casacore::Double> pixel_point;
            if (WorldPointToImagePixels(world_point, output_csys, pixel_point)) {
                // Add to x and y Vectors
                int index(i / 2);
                x(index) = pixel_point(0);
                y(index) = pixel_point(1);
            } else {
                spdlog::error("Error converting region type {} to image pixels.", type);
                return record;
            }
        }

        if (type == CARTA::RegionType::POLYGON) {
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
        spdlog::error("Error converting region type () to image: {}", type, err.getMesg());
    }

    return record;
}

casacore::TableRecord RegionConverter::GetRotboxRecord(std::shared_ptr<casacore::CoordinateSystem> output_csys) {
    // Convert control points for rotated box-type region (ignore rotation) to output image pixels
    // in format of LCPolygon::toRecord().
    casacore::TableRecord record;
    try {
        // Get 4 corner points (unrotated) in pixel coordinates
        casacore::Vector<casacore::Double> x, y;
        bool apply_rotation(false);
        if (_region_state.GetRectangleCorners(x, y, apply_rotation)) {
            // Convert corners to reference world coords.
            // Cannot use wcs control points because rotation was applied.
            int num_axes(_reference_coord_sys->nPixelAxes());
            auto num_points(x.size());
            casacore::Matrix<casacore::Double> pixel_coords(num_axes, num_points);
            casacore::Matrix<casacore::Double> world_coords(num_axes, num_points);
            pixel_coords = 0.0;
            pixel_coords.row(0) = x;
            pixel_coords.row(1) = y;
            casacore::Vector<casacore::Bool> failures;
            if (!_reference_coord_sys->toWorldMany(world_coords, pixel_coords, failures)) {
                spdlog::error("Error converting rectangle pixel coordinates to world.");
                return record;
            }

            // Convert reference world coord points to output pixel points
            casacore::Vector<casacore::Double> ref_x_world = world_coords.row(0);
            casacore::Vector<casacore::Double> ref_y_world = world_coords.row(1);
            casacore::Vector<casacore::String> ref_world_units = _reference_coord_sys->worldAxisUnits();
            casacore::Vector<casacore::Float> out_x_pix(num_points), out_y_pix(num_points);
            for (size_t i = 0; i < num_points; i++) {
                // Reference world point as Quantity
                std::vector<casacore::Quantity> ref_world_point;
                ref_world_point.push_back(casacore::Quantity(ref_x_world(i), ref_world_units(0)));
                ref_world_point.push_back(casacore::Quantity(ref_y_world(i), ref_world_units(1)));
                // Convert to output pixel point
                casacore::Vector<casacore::Double> out_pixel_point;
                if (WorldPointToImagePixels(ref_world_point, output_csys, out_pixel_point)) {
                    out_x_pix(i) = out_pixel_point(0);
                    out_y_pix(i) = out_pixel_point(1);
                } else {
                    spdlog::error("Error converting rectangle coordinates to image.");
                    return record;
                }
            }

            // Add fields for this region type
            record.define("name", "LCPolygon");
            record.define("x", out_x_pix);
            record.define("y", out_y_pix);
        }
    } catch (const casacore::AipsError& err) {
        spdlog::error("Error converting rectangle to image: {}", err.getMesg());
    }
    return record;
}

casacore::TableRecord RegionConverter::GetEllipseRecord(std::shared_ptr<casacore::CoordinateSystem> output_csys) {
    // Convert wcs points to output image in format of LCEllipsoid::toRecord()
    casacore::TableRecord record;
    casacore::Vector<casacore::Float> center(2), radii(2); // for record

    // Center point
    std::vector<casacore::Quantity> ref_world_point(2);
    ref_world_point[0] = _wcs_control_points[0];
    ref_world_point[1] = _wcs_control_points[1];
    casacore::Vector<casacore::Double> out_pixel_point;

    try {
        if (WorldPointToImagePixels(ref_world_point, output_csys, out_pixel_point)) {
            center(0) = out_pixel_point(0);
            center(1) = out_pixel_point(1);

            // Convert radii to output world units, then to pixels using increment
            casacore::Quantity bmaj = _wcs_control_points[2];
            casacore::Quantity bmin = _wcs_control_points[3];
            casacore::Vector<casacore::Double> out_increments(output_csys->increment());
            casacore::Vector<casacore::String> out_units(output_csys->worldAxisUnits());
            bmaj.convert(out_units(0));
            bmin.convert(out_units(1));
            radii(0) = fabs(bmaj.getValue() / out_increments(0));
            radii(1) = fabs(bmin.getValue() / out_increments(1));

            // Add fields for this region type
            record.define("name", "LCEllipsoid");
            record.define("center", center);
            record.define("radii", radii);

            // LCEllipsoid measured from major (x) axis
            casacore::Quantity theta = casacore::Quantity(_region_state.rotation + 90.0, "deg");
            theta.convert("rad");
            record.define("theta", theta.getValue());
        } else {
            spdlog::error("Incompatible coordinate systems for ellipse conversion.");
        }
    } catch (const casacore::AipsError& err) {
        spdlog::error("Error converting ellipse to image: {}", err.getMesg());
    }

    return record;
}

// *************************************************************************
// Utilities for pixel/world conversion

bool RegionConverter::PointsToImagePixels(const std::vector<CARTA::Point>& points, std::shared_ptr<casacore::CoordinateSystem> output_csys,
    casacore::Vector<casacore::Double>& x, casacore::Vector<casacore::Double>& y) {
    // Convert pixel coords in reference image (points) to pixel coords in output image coordinate system (x and y).
    // ref pixels -> ref world -> output world -> output pixels
    bool converted(true);
    try {
        // Convert each pixel point to output csys pixel
        size_t npoints(points.size());
        x.resize(npoints);
        y.resize(npoints);

        for (auto i = 0; i < npoints; ++i) {
            // Convert pixel to world (reference image) [x, y]
            std::vector<casacore::Quantity> world_point;
            if (CartaPointToWorld(points[i], world_point)) {
                // Convert world to pixel (output image) [x, y]
                casacore::Vector<casacore::Double> pixel_point;
                if (WorldPointToImagePixels(world_point, output_csys, pixel_point)) {
                    x(i) = pixel_point(0);
                    y(i) = pixel_point(1);
                } else { // world to pixel failed
                    spdlog::error("Error converting region to output image pixel coords.");
                    converted = false;
                    break;
                }
            } else { // pixel to world failed
                spdlog::error("Error converting region to reference image world coords.");
                converted = false;
                break;
            }
        }
    } catch (const casacore::AipsError& err) {
        spdlog::error("Error converting region to output image: {}", err.getMesg());
        converted = false;
    }

    return converted;
}

bool RegionConverter::WorldPointToImagePixels(std::vector<casacore::Quantity>& world_point,
    std::shared_ptr<casacore::CoordinateSystem> output_csys, casacore::Vector<casacore::Double>& pixel_point) {
    // Convert reference world-coord point to output pixel-coord point: ref world -> output world -> output pixels.
    // Both images must have direction coordinates or linear coordinates.
    // Returns pixel points with success or throws exception (catch in calling function).
    bool success(false);
    if (_reference_coord_sys->hasDirectionCoordinate() && output_csys->hasDirectionCoordinate()) {
        // Input and output direction reference frames
        casacore::MDirection::Types reference_dir_type = _reference_coord_sys->directionCoordinate().directionType();
        casacore::MDirection::Types output_dir_type = output_csys->directionCoordinate().directionType();

        // Convert world point from reference to output coord sys
        casacore::MDirection world_direction(world_point[0], world_point[1], reference_dir_type);
        if (reference_dir_type != output_dir_type) {
            world_direction = casacore::MDirection::Convert(world_direction, output_dir_type)();
        }

        // Convert output world point to pixel point
        output_csys->directionCoordinate().toPixel(pixel_point, world_direction);
        success = true;
    } else if (_reference_coord_sys->hasLinearCoordinate() && output_csys->hasLinearCoordinate()) {
        // Get linear axes indices
        auto indices = output_csys->linearAxesNumbers();
        if (indices.size() != 2) {
            return false;
        }
        // Input and output linear frames
        casacore::Vector<casacore::String> output_units = output_csys->worldAxisUnits();
        casacore::Vector<casacore::Double> world_point_value(output_csys->nWorldAxes(), 0);
        world_point_value(indices(0)) = world_point[0].get(output_units(indices(0))).getValue();
        world_point_value(indices(1)) = world_point[1].get(output_units(indices(1))).getValue();

        // Convert world point to output pixel point
        casacore::Vector<casacore::Double> tmp_pixel_point;
        output_csys->toPixel(tmp_pixel_point, world_point_value);

        // Only fill the pixel coordinate results
        pixel_point.resize(2);
        pixel_point(0) = tmp_pixel_point(indices(0));
        pixel_point(1) = tmp_pixel_point(indices(1));
        success = true;
    }
    return success;
}
