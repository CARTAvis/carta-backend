/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# RegionImportExportUtil.cc: functions common to CRTF and DS9 import and export

#include "RegionImportExportUtil.h"

#include <algorithm>
#include <cmath>
#include <casacore/casa/BasicMath/Math.h>

#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>
#include <casacore/measures/Measures/MCDirection.h>

namespace carta {

bool PixelEllipseAxesToWorld(const casacore::CoordinateSystem& coord_sys, double first, double second, casacore::Quantity& rotation,
    std::vector<casacore::Quantity>& axes) {
    // The restored first radius follows rotation, measured from the horizontal axis.
    const double angle = rotation.get("rad").getValue();
    const auto x_scale = coord_sys.toWorldLength(1, 0);
    const auto y_scale = coord_sys.toWorldLength(1, 1);
    const double sx = x_scale.getValue();
    if (!x_scale.isConform(y_scale.getUnit())) {
        return false;
    }
    const double sy = y_scale.get(x_scale.getUnit()).getValue();
    if (casacore::near(sx, sy)) {
        axes.push_back(casacore::Quantity(first * sx, x_scale.getUnit()));
        axes.push_back(casacore::Quantity(second * sy, x_scale.getUnit()));
    } else {
        // Unequal pixel scales change both the principal radii and the angle of a rotated ellipse.
        const double c = std::cos(angle), s = std::sin(angle);
        const double xx = sx * sx * (first * first * c * c + second * second * s * s);
        const double yy = sy * sy * (first * first * s * s + second * second * c * c);
        const double xy = sx * sy * (first * first - second * second) * s * c;
        const double difference = std::hypot(xx - yy, 2 * xy);
        const bool circular = difference <= 1e-12 * (xx + yy);
        const double axis_difference = circular ? 0 : difference;
        axes.push_back(casacore::Quantity(std::sqrt((xx + yy + axis_difference) / 2), x_scale.getUnit()));
        axes.push_back(casacore::Quantity(std::sqrt(std::max(0.0, (xx + yy - axis_difference) / 2)), x_scale.getUnit()));
        rotation = casacore::Quantity(circular ? 0 : std::atan2(2 * xy, xx - yy) / 2, "rad");
        rotation.convert("deg");
    }
    return true;
}

bool IsApproximatelyCircular(double radius_x, double radius_y) {
    return std::isfinite(radius_x) && std::isfinite(radius_y) && radius_x > 0.0 && radius_y > 0.0 &&
           std::abs(radius_x - radius_y) <= 1e-6 * std::max(std::abs(radius_x), std::abs(radius_y));
}

std::unordered_map<CARTA::RegionType, std::string> GetRegionTypeNames(CARTA::FileType region_file_type) {
    if (region_file_type == CARTA::CRTF) {
        region_names[CARTA::RegionType::POINT] = "symbol";
        region_names[CARTA::RegionType::RECTANGLE] = "centerbox";
        region_names[CARTA::RegionType::ANNULUS] = "annulus";
        region_names[CARTA::RegionType::POLYGON] = "poly";
        region_names[CARTA::RegionType::ANNPOINT] = "ann symbol";
        region_names[CARTA::RegionType::ANNLINE] = "ann line";
        region_names[CARTA::RegionType::ANNPOLYLINE] = "ann polyline";
        region_names[CARTA::RegionType::ANNRECTANGLE] = "ann centerbox";
        region_names[CARTA::RegionType::ANNELLIPSE] = "ann ellipse";
        region_names[CARTA::RegionType::ANNPOLYGON] = "ann poly";
        region_names[CARTA::RegionType::ANNVECTOR] = "vector";
        region_names[CARTA::RegionType::ANNTEXT] = "text";
    } else if (region_file_type == CARTA::DS9_REG) {
        region_names[CARTA::RegionType::POINT] = "point";
        region_names[CARTA::RegionType::RECTANGLE] = "box";
        region_names[CARTA::RegionType::POLYGON] = "polygon";
        region_names[CARTA::RegionType::ANNPOINT] = "# point";
        region_names[CARTA::RegionType::ANNLINE] = "# line";
        region_names[CARTA::RegionType::ANNPOLYLINE] = "# polyline";
        region_names[CARTA::RegionType::ANNRECTANGLE] = "# box";
        region_names[CARTA::RegionType::ANNELLIPSE] = "# ellipse";
        region_names[CARTA::RegionType::ANNPOLYGON] = "# polygon";
        region_names[CARTA::RegionType::ANNVECTOR] = "# vector";
        region_names[CARTA::RegionType::ANNTEXT] = "# text";
    }
    return region_names;
}

std::string GetImageDirectionFrame(std::shared_ptr<casacore::CoordinateSystem> coord_sys) {
    std::string dir_frame;
    if (coord_sys) {
        if (coord_sys->hasDirectionCoordinate()) {
            casacore::MDirection::Types mdir_type = coord_sys->directionCoordinate().directionType();
            dir_frame = casacore::MDirection::showType(mdir_type);
        } else if (coord_sys->hasLinearCoordinate()) {
            dir_frame = "linear";
        }
    }
    return dir_frame;
}

bool ConvertPointToPixels(std::shared_ptr<casacore::CoordinateSystem> coord_sys, std::string& region_frame,
    std::vector<casacore::Quantity>& point, casacore::Vector<casacore::Double>& pixel_coords) {
    if (point.size() != 2) {
        return false;
    }

    // x and y must have matched pixel/world types
    casacore::String unit0(point[0].getUnit()), unit1(point[1].getUnit());
    bool x_is_pix = unit0.contains("pix");
    bool y_is_pix = unit1.contains("pix");
    if (x_is_pix != y_is_pix) {
        return false;
    }

    // If unit is pixels, just get values
    if (x_is_pix) {
        pixel_coords.resize(2);
        pixel_coords(0) = point[0].getValue();
        pixel_coords(1) = point[1].getValue();
        return true;
    }

    // Convert world to pixel coords
    bool converted_to_pixel(false);
    if (coord_sys->hasDirectionCoordinate()) {
        try {
            casacore::MDirection::Types image_direction_type = coord_sys->directionCoordinate().directionType();

            casacore::MDirection::Types region_direction_type;
            if (region_frame.empty()) {
                region_direction_type = image_direction_type;
            } else {
                casacore::MDirection::getType(region_direction_type, region_frame);
            }

            // Make MDirection from wcs parameter
            casacore::MDirection direction(point[0], point[1], region_direction_type);

            // Convert to image direction
            if (region_direction_type != image_direction_type) {
                direction = casacore::MDirection::Convert(direction, image_direction_type)();
            }

            // Convert world to pixel coordinates. Uses wcslib wcss2p(); pixels are not fractional
            converted_to_pixel = coord_sys->directionCoordinate().toPixel(pixel_coords, direction);
        } catch (const casacore::AipsError& err) {
            return converted_to_pixel;
        }
    }

    return converted_to_pixel;
}

std::string FormatColor(const std::string& color) {
    std::string hex_color(color);
    if (color[0] == '#') {
        // Do conversion without prefix
        hex_color = color.substr(1);
    }

    // Check if can convert entire string to hex number
    char* endptr(nullptr);
    if (std::strtoul(hex_color.c_str(), &endptr, 16) && (*endptr == '\0')) {
        // Capitalize and add prefix
        std::transform(hex_color.begin(), hex_color.end(), hex_color.begin(), ::toupper);
        hex_color = "#" + hex_color;
    }

    return hex_color;
}

} // namespace carta
