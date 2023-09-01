/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// LineBoxRegions.cc: Calculate box regions along line to approximate line with width.

#include "LineBoxRegions.h"

#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>

#include "Logger/Logger.h"

namespace carta {

bool LineBoxRegions::GetLineBoxRegions(const RegionState& line_region_state, std::shared_ptr<casacore::CoordinateSystem> line_coord_sys,
    int line_width, casacore::Quantity& region_increment, std::vector<RegionState>& region_states, std::string& message) {
    // Generate box regions to approximate a line with a width (pixels).
    // Return parameters: increment (angular spacing of boxes), per-region RegionState, message if failure.
    // Returns whether regions were set successfully.

    // Check input parameters
    if (line_width < 1 || line_width > 20) {
        message = fmt::format("Invalid averaging width: {}.", line_width);
        spdlog::error(message);
        return false;
    }

    // Line box regions are set with line's reference image coordinate system. Need direction coordinate for angular separation.
    if (!line_coord_sys->hasDirectionCoordinate()) {
        message = "Cannot approximate line with no direction coordinate.";
        return false;
    }

    double increment;
    bool regions_complete = GetFixedPixelRegions(line_region_state, line_coord_sys, line_width, increment, region_states);

    if (regions_complete) {
        spdlog::debug("Using fixed pixel increment for line profiles.");
    } else {
        regions_complete = GetFixedAngularRegions(line_region_state, line_coord_sys, line_width, increment, region_states, message);
        if (regions_complete) {
            spdlog::debug("Using fixed angular increment for line profiles.");
        }
    }

    if (regions_complete) {
        region_increment = AdjustIncrementUnit(increment, region_states.size());
    }

    return regions_complete;
}

bool LineBoxRegions::GetFixedPixelRegions(const RegionState& line_region_state, std::shared_ptr<casacore::CoordinateSystem> line_coord_sys,
    int line_width, double& increment, std::vector<RegionState>& region_states) {
    // Calculate box regions along line with fixed pixel spacing.
    // Returns false if pixel centers are not uniformly spaced in world coordinates.
    auto line_points = line_region_state.control_points;

    // Determine line or polyline.  If line, start at center, else start at one end.
    size_t num_lines(line_points.size() - 1);

    if (num_lines == 1) {
        // Get box centers along line; start at line center and add offsets
        std::vector<double> line_start({line_points[0].x(), line_points[0].y()});
        std::vector<double> line_end({line_points[1].x(), line_points[1].y()});

        // Number of pixels in line, to determine number of offsets
        auto dx_pixels = line_end[0] - line_start[0];
        auto dy_pixels = line_end[1] - line_start[1];
        double pixel_length = sqrt((dx_pixels * dx_pixels) + (dy_pixels * dy_pixels));
        auto num_offsets = std::max(lround((pixel_length - 1.0) / 2.0), (long)0);

        // Offset range [-offset, offset] from center pixel
        std::vector<std::vector<double>> box_centers((num_offsets * 2) + 1);

        // Set center pixel at center index
        double center_x((line_start[0] + line_end[0]) / 2.0), center_y((line_start[1] + line_end[1]) / 2.0);
        std::vector<double> center_point({center_x, center_y});
        auto center_idx = num_offsets;
        box_centers[center_idx] = center_point;

        // Rotation angle of line, applied to get next pixel
        float rotation = GetLineRotation(line_start, line_end);
        float cos_x = cos(rotation * M_PI / 180.0f);
        float sin_x = sin(rotation * M_PI / 180.0f);

        // Set box centers in pos and neg direction from line center out
        for (auto ioffset = 1; ioffset <= num_offsets; ++ioffset) {
            // Positive offset toward line_start, negative offset toward line_end
            box_centers[center_idx + ioffset] = {center_x + (ioffset * cos_x), center_y + (ioffset * sin_x)};
            box_centers[center_idx - ioffset] = {center_x - (ioffset * cos_x), center_y - (ioffset * sin_x)};
        }

        // Get region state for each box
        auto num_regions = box_centers.size();

        if (num_regions == 1) {
            // Set increment for 1 pixel
            auto xlength = line_coord_sys->toWorldLength(cos_x, 0).get("arcsec").getValue();
            auto ylength = line_coord_sys->toWorldLength(sin_x, 1).get("arcsec").getValue();
            increment = sqrt((xlength * xlength) + (ylength * ylength));
        } else if (!CheckLinearOffsets(box_centers, line_coord_sys, increment)) {
            // Check if angular separation of pixels (box centers) is linear
            spdlog::debug("Fixed pixel offsets not linear");
            return false;
        }

        // Overlap regions if not vertical or horizontal line
        float height = (fmod(rotation, 90.0) == 0.0 ? 1.0 : 3.0);

        // Set box regions from centers, line width, height
        for (size_t iregion = 0; iregion < num_regions; ++iregion) {
            // Set temporary region state
            std::vector<CARTA::Point> control_points;
            control_points.push_back(Message::Point(box_centers[iregion]));
            control_points.push_back(Message::Point(line_width, height));
            RegionState box_region_state(line_region_state.reference_file_id, CARTA::RegionType::RECTANGLE, control_points, rotation);
            region_states.push_back(box_region_state);
        }
    } else {
        bool trim_line(false); // Whether to skip first region after vertex
        int profile_idx(0);

        for (size_t iline = 0; iline < num_lines; iline++) {
            std::vector<double> line_start({line_points[iline].x(), line_points[iline].y()});
            std::vector<double> line_end({line_points[iline + 1].x(), line_points[iline + 1].y()});

            // Number of regions = pixel length of line
            auto dx_pixels = line_end[0] - line_start[0];
            auto dy_pixels = line_end[1] - line_start[1];
            double pixel_length = sqrt((dx_pixels * dx_pixels) + (dy_pixels * dy_pixels));
            int num_regions = lround(pixel_length) + 1;

            int start(0);
            if (trim_line) {
                spdlog::debug("Trimming line segment {}", iline);
                start = 1;
            }

            // Rotation angle of line segment, applied to get next pixel
            float rotation = GetLineRotation(line_start, line_end);
            float cos_x = cos(rotation * M_PI / 180.0f);
            float sin_x = sin(rotation * M_PI / 180.0f);

            std::vector<std::vector<double>> box_centers;
            for (int iregion = start; iregion < num_regions; ++iregion) {
                box_centers.push_back({line_start[0] - (iregion * cos_x), line_start[1] - (iregion * sin_x)});
            }

            num_regions = box_centers.size(); // will adjust if trimmed

            if (num_regions == 0) {
                spdlog::debug("Line segment {} contains no pixels", iline);
                continue;
            } else if (num_regions == 1) {
                // Set increment for 1 pixel (usually set in CheckLinearOffsets)
                auto xlength = line_coord_sys->toWorldLength(cos_x, 0).get("arcsec").getValue();
                auto ylength = line_coord_sys->toWorldLength(sin_x, 1).get("arcsec").getValue();
                increment = sqrt((xlength * xlength) + (ylength * ylength));
            } else if (!CheckLinearOffsets(box_centers, line_coord_sys, increment)) {
                spdlog::debug("Fixed pixel offsets not linear");
                return false;
            }

            // Overlap regions if not vertical or horizontal line
            float height = (fmod(rotation, 90.0) == 0.0 ? 1.0 : 3.0);

            for (int iregion = 0; iregion < num_regions; ++iregion) {
                // Set box region
                std::vector<CARTA::Point> control_points;
                control_points.push_back(Message::Point(box_centers[iregion]));
                control_points.push_back(Message::Point(line_width, height));
                RegionState box_region_state(line_region_state.reference_file_id, CARTA::RegionType::RECTANGLE, control_points, rotation);
                region_states.push_back(box_region_state);
            }

            // Check whether to trim next line's starting point
            if (box_centers.empty()) {
                trim_line = false;
            } else {
                trim_line = (GetPointSeparation(line_coord_sys, box_centers.back(), line_end) < (0.5 * increment));
            }
        }
    }

    return !region_states.empty();
}

float LineBoxRegions::GetLineRotation(const std::vector<double>& line_start, const std::vector<double>& line_end) {
    // Not set on line region import, or line segment of polyline
    // Angle from x-axis in deg
    return atan2((line_start[1] - line_end[1]), (line_start[0] - line_end[0])) * 180.0 / M_PI;
}

bool LineBoxRegions::CheckLinearOffsets(
    const std::vector<std::vector<double>>& box_centers, std::shared_ptr<casacore::CoordinateSystem> coord_sys, double& increment) {
    // Check whether separation between box centers is linear.
    size_t num_centers(box_centers.size()), num_separation(0);
    double min_separation(0.0), max_separation(0.0);
    double total_separation(0.0);
    double tolerance = GetSeparationTolerance(coord_sys);

    // Check angular separation between centers
    for (size_t i = 0; i < num_centers - 1; ++i) {
        double center_separation = GetPointSeparation(coord_sys, box_centers[i], box_centers[i + 1]);

        // Check separation
        if (center_separation > 0) {
            if (i == 0) {
                min_separation = max_separation = center_separation;
            } else {
                min_separation = (center_separation < min_separation) ? center_separation : min_separation;
                max_separation = (center_separation > max_separation) ? center_separation : max_separation;
            }

            if ((max_separation - min_separation) > tolerance) { // nonlinear increment
                return false;
            }

            total_separation += center_separation; // accumulate for mean
            ++num_separation;
        }
    }

    increment = total_separation / double(num_separation); // calculate mean separation
    return true;
}

double LineBoxRegions::GetPointSeparation(
    std::shared_ptr<casacore::CoordinateSystem> coord_sys, const std::vector<double>& point1, const std::vector<double>& point2) {
    // Returns angular separation in arcsec. Both points must be inside image or returns zero (use GetWorldLength instead, not as accurate).
    double separation(0.0);
    casacore::Vector<double> const point1V(point1), point2V(point2);
    std::lock_guard<std::mutex> guard(_mvdir_mutex);
    try {
        casacore::MVDirection mvdir1 = coord_sys->directionCoordinate().toWorld(point1V);
        casacore::MVDirection mvdir2 = coord_sys->directionCoordinate().toWorld(point2V);
        separation = mvdir1.separation(mvdir2, "arcsec").getValue();
    } catch (casacore::AipsError& err) {
        // invalid pixel coordinates - outside image
    }

    return separation;
}

double LineBoxRegions::GetSeparationTolerance(std::shared_ptr<casacore::CoordinateSystem> csys) {
    // Return 1% of CDELT2 in arcsec
    auto cdelt = csys->increment();
    auto cunit = csys->worldAxisUnits();
    casacore::Quantity cdelt2(cdelt[1], cunit[1]);
    return abs(cdelt2.get("arcsec").getValue()) * 0.01;
}

bool LineBoxRegions::GetFixedAngularRegions(const RegionState& line_region_state,
    std::shared_ptr<casacore::CoordinateSystem> line_coord_sys, int line_width, double& increment, std::vector<RegionState>& region_states,
    std::string& message) {
    // Calculate polygon regions along line (may not be box in non-linear csys) with fixed angular spacing.
    // Returns false if regions failed.
    auto control_points = line_region_state.control_points;

    // Determine line or polyline.  If line, start at center, else start at one end.
    size_t num_lines(control_points.size() - 1);

    // Target increment is CDELT2, target width is width * CDELT2
    auto inc2 = line_coord_sys->increment()(1);
    auto cunit2 = line_coord_sys->worldAxisUnits()(1);
    casacore::Quantity cdelt2(inc2, cunit2);
    increment = abs(cdelt2.get("arcsec").getValue());
    double tolerance = 0.1 * increment;
    double angular_width = line_width * increment;

    if (num_lines == 1) {
        // Use pixel center of line and keep it centered
        std::vector<double> line_start({control_points[0].x(), control_points[0].y()});
        std::vector<double> line_end({control_points[1].x(), control_points[1].y()});

        double line_separation = GetPointSeparation(line_coord_sys, line_start, line_end);

        if (!line_separation) {
            // endpoint(s) out of image and coordinate system
            message = "Line endpoints do not have valid world coordinates.";
            return false;
        }

        // Number of region profiles determined by increments in line length.
        auto num_increments = line_separation / increment;
        int num_offsets = lround(num_increments / 2.0);
        int num_regions = num_offsets * 2;
        if (num_regions == 0) {
            message = "Line is shorter than target increment.";
            return false;
        }

        // Start at center and add points in each offset direction
        std::vector<std::vector<double>> line_points(num_regions + 1); // points are start/end of each box
        std::vector<double> line_center({(line_start[0] + line_end[0]) / 2.0, (line_start[1] + line_end[1]) / 2.0});
        line_points[num_offsets] = line_center;

        // Copy center for offsets
        std::vector<double> pos_box_start({line_center[0], line_center[1]}), neg_box_start({line_center[0], line_center[1]});

        // Get points along line from center out with increment spacing to set regions
        for (int ioffset = 1; ioffset <= num_offsets; ++ioffset) {
            // Each box height (box_start to box_end) is variable to be fixed angular spacing.
            // Find ends of box regions, at increment from start of box in positive offset direction.
            if (!pos_box_start.empty()) {
                std::vector<double> pos_box_end =
                    FindPointAtTargetSeparation(line_coord_sys, pos_box_start, line_start, increment, tolerance);
                line_points[num_offsets + ioffset] = pos_box_end;
                pos_box_start = pos_box_end; // end of this box is start of next box
            }

            // Find ends of box regions, at increment from start of box in negative offset direction.
            if (!neg_box_start.empty()) {
                std::vector<double> neg_box_end =
                    FindPointAtTargetSeparation(line_coord_sys, neg_box_start, line_end, increment, tolerance);
                line_points[num_offsets - ioffset] = neg_box_end;
                neg_box_start = neg_box_end; // end of this box is start of next box
            }
        }

        int start_idx, end_idx;                                 // for start and end of overlapping box regions
        float rotation = GetLineRotation(line_start, line_end); // for RegionState

        for (int iregion = 0; iregion < num_regions; ++iregion) {
            // Set index for start and end of region to overlap regions: 3 boxes wide except at ends
            start_idx = (iregion == 0 ? iregion : iregion - 1);
            end_idx = (iregion == (num_regions - 1) ? iregion + 1 : iregion + 2);
            std::vector<double> region_start(line_points[start_idx]), region_end(line_points[end_idx]);
            RegionState polygon_region_state; // corners of box but not a box since not uniform

            if (!region_start.empty() && !region_end.empty()) {
                // Find box corners and set polygon region. If empty, part of line off image.
                polygon_region_state = GetPolygonRegionState(line_coord_sys, line_region_state.reference_file_id, region_start, region_end,
                    line_width, angular_width, rotation, tolerance);
            }

            region_states.push_back(polygon_region_state);
        }
    } else {
        // Polyline profiles, for spatial profile only
        bool trim_line(false); // Whether to skip first region after vertex
        int profile_row(0);

        for (size_t iline = 0; iline < num_lines; iline++) {
            std::vector<double> line_start({control_points[iline].x(), control_points[iline].y()});
            std::vector<double> line_end({control_points[iline + 1].x(), control_points[iline + 1].y()});

            // Angular length of line (arcsec)
            double line_separation = GetPointSeparation(line_coord_sys, line_start, line_end);

            if (!line_separation) {
                // endpoint(s) out of image and coordinate system
                message = "Polyline segment endpoints do not have valid world coordinates.";
                return false;
            }

            // Number of regions in segment is (angular length / increment)
            int num_regions = lround(line_separation / increment);
            if (num_regions == 0) {
                spdlog::debug("Polyline segment {} is shorter than target increment.", iline);
                continue;
            }

            // Use vector instead of PointXy for "no point" when reach end of line
            std::vector<std::vector<double>> line_points;
            line_points.push_back(line_start);

            for (int iregion = 1; iregion < num_regions + 1; ++iregion) {
                // Find next point (next box end) along line at target separation
                std::vector<double> next_point =
                    FindPointAtTargetSeparation(line_coord_sys, line_points.back(), line_end, increment, tolerance);

                if (next_point.empty()) {
                    break;
                } else {
                    line_points.push_back(next_point);
                }
            }

            num_regions = line_points.size() - 1;
            int start_idx, end_idx;                                 // for start and end of overlapping box regions
            float rotation = GetLineRotation(line_start, line_end); // for temporary box RegionState

            for (int iregion = 0; iregion < num_regions; ++iregion) {
                if (trim_line) {
                    spdlog::debug("Polyline segment {} trimmed", iline);
                    trim_line = false;
                    continue;
                }

                // Set index for start and end of region to overlap regions: 3 boxes wide except at ends
                start_idx = (iregion == 0 ? iregion : iregion - 1);
                end_idx = (iregion == (num_regions - 1) ? iregion + 1 : iregion + 2);
                std::vector<double> region_start(line_points[start_idx]), region_end(line_points[end_idx]);
                RegionState polygon_region_state; // set box as polygon

                if (!region_start.empty() && !region_end.empty()) {
                    // If points empty, part of line off image
                    // Set polygon region; mutex is locked in function while determining polygon corners.
                    polygon_region_state = GetPolygonRegionState(line_coord_sys, line_region_state.reference_file_id, region_start,
                        region_end, line_width, angular_width, rotation, tolerance);
                }

                region_states.push_back(polygon_region_state);
            }

            // Check whether to trim next line's starting point
            if (line_points.back().empty()) {
                trim_line = false;
            } else {
                trim_line = (GetPointSeparation(line_coord_sys, line_points.back(), line_end) < (0.5 * increment));
            }
        } // line segments loop
    }

    return !region_states.empty();
}

std::vector<double> LineBoxRegions::FindPointAtTargetSeparation(std::shared_ptr<casacore::CoordinateSystem> coord_sys,
    const std::vector<double>& start_point, const std::vector<double>& end_point, double target_separation, double tolerance) {
    // Find point on line described by start and end points which is at target separation in arcsec (within tolerance) of start point.
    // Return point [x, y] in pixel coordinates.  Vector is empty if DirectionCoordinate conversion fails.
    std::vector<double> target_point;

    // Do binary search of line, finding midpoints until target separation is reached.
    // Check endpoint separation
    auto separation = GetPointSeparation(coord_sys, start_point, end_point);
    if (separation < target_separation) {
        // Line is shorter than target separation
        return target_point;
    }

    // Set progressively smaller range start-end which contains target point by testing midpoints
    std::vector<double> start({start_point[0], start_point[1]});
    std::vector<double> end({end_point[0], end_point[1]});

    std::vector<double> last_end, midpoint(2);
    int limit(0);
    auto delta = separation - target_separation;

    while (abs(delta) > tolerance) {
        if (limit++ == 1000) { // should not hit this
            break;
        }

        if (delta > 0) {
            // Separation too large, get midpoint of start/end
            midpoint[0] = (start[0] + end[0]) / 2;
            midpoint[1] = (start[1] + end[1]) / 2;
            last_end = end;
            end = midpoint;
        } else {
            // Separation too small: get midpoint of end/last_end
            midpoint[0] = (end[0] + last_end[0]) / 2;
            midpoint[1] = (end[1] + last_end[1]) / 2;
            start = end;
            end = midpoint;
        }

        // Get separation between start point and new endpoint
        separation = GetPointSeparation(coord_sys, start_point, end);
        delta = separation - target_separation;
    }

    if (abs(delta) <= tolerance) {
        target_point.push_back(end[0]);
        target_point.push_back(end[1]);
    }

    return target_point;
}

RegionState LineBoxRegions::GetPolygonRegionState(std::shared_ptr<casacore::CoordinateSystem> coord_sys, int file_id,
    const std::vector<double>& box_start, const std::vector<double>& box_end, int pixel_width, double angular_width, float line_rotation,
    double tolerance) {
    // Return RegionState for polygon region describing a box with given start and end (pixel coords) on line with rotation.
    // Get box corners with angular width to get box corners.
    // Polygon control points are corners of this box.
    // Used for widefield images with nonlinear spacing, where pixel center is not angular center so cannot use rectangle definition.
    double half_width = angular_width / 2.0;

    // Perpendicular to line
    float cos_x = cos((line_rotation + 90.0) * M_PI / 180.0f);
    float sin_x = sin((line_rotation + 90.0) * M_PI / 180.0f);

    // Control points for polygon region state
    std::vector<CARTA::Point> control_points(4);

    // Create line perpendicular to line (along "width axis") at box start to find box corners
    // Endpoint in positive direction width*2 pixels out from box start
    std::vector<double> target_end({box_start[0] - (pixel_width * 2 * cos_x), box_start[1] - (pixel_width * 2 * sin_x)});
    std::vector<double> corner = FindPointAtTargetSeparation(coord_sys, box_start, target_end, half_width, tolerance);
    if (corner.empty()) {
        return RegionState();
    }
    control_points[0] = Message::Point(corner);

    // Endpoint in negative direction width*2 pixels out from box start
    target_end = {box_start[0] + (pixel_width * 2 * cos_x), box_start[1] + (pixel_width * 2 * sin_x)};
    corner = FindPointAtTargetSeparation(coord_sys, box_start, target_end, half_width, tolerance);
    if (corner.empty()) {
        return RegionState();
    }
    control_points[3] = Message::Point(corner);

    // Find box corners from box end
    // Endpoint in positive direction width*2 pixels out from box end
    target_end = {box_end[0] - (pixel_width * 2 * cos_x), box_end[1] - (pixel_width * 2 * sin_x)};
    corner = FindPointAtTargetSeparation(coord_sys, box_end, target_end, half_width, tolerance);
    if (corner.empty()) {
        return RegionState();
    }
    control_points[1] = Message::Point(corner);

    // Endpoint in negative direction width*2 pixels out from box end
    target_end = {box_end[0] + (pixel_width * 2 * cos_x), box_end[1] + (pixel_width * 2 * sin_x)};
    corner = FindPointAtTargetSeparation(coord_sys, box_end, target_end, half_width, tolerance);
    if (corner.empty()) {
        return RegionState();
    }
    control_points[2] = Message::Point(corner);

    float polygon_rotation(0.0);
    RegionState region_state = RegionState(file_id, CARTA::RegionType::POLYGON, control_points, polygon_rotation);
    return region_state;
}

casacore::Quantity LineBoxRegions::AdjustIncrementUnit(double offset_increment, size_t num_offsets) {
    // Given offset increment in arcsec, adjust to:
    // - milliarcsec if length < 2 milliarcsec
    // - arcsec if 2 milliarcsec <= length < 2 arcmin
    // - arcminute if 2 arcmin <= length < 2 deg
    // - deg if 2 deg <= length
    // Returns increment as a Quantity with value and unit
    casacore::Quantity increment(offset_increment, "arcsec");
    auto offset_length = offset_increment * num_offsets;

    if ((offset_length * 1.0e3) < 2.0) {
        increment = increment.get("marcsec");
    } else if ((offset_length / 60.0) >= 2.0) {
        if ((offset_length / 3600.0) < 2.0) {
            increment = increment.get("arcmin");
        } else {
            increment = increment.get("deg");
        }
    }

    return increment;
}

} // namespace carta
