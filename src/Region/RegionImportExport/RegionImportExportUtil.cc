/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# RegionImportExportUtil.cc: functions common to CRTF and DS9 import and export

#include "RegionImportExportUtil.h"

#include <casacore/casa/BasicMath/Math.h>
#include <algorithm>
#include <cmath>

#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>
#include <casacore/measures/Measures/MCDirection.h>

namespace carta {

namespace {
// Local pixel-to-world matrix. Celestial axes are east/north in radians; signed
// offsets retain WCS rotation, reflection, projection, and coordinate-frame conversion.
bool PixelToWorldEllipseTransform(const casacore::CoordinateSystem& coord_sys, const casacore::Vector<casacore::Double>& center,
    const std::string& world_frame, casacore::Matrix<casacore::Double>& transform, casacore::Unit& unit) {
    transform.resize(2, 2);
    if (coord_sys.hasDirectionCoordinate()) {
        const auto& direction = coord_sys.directionCoordinate();
        auto frame = direction.directionType();
        if (!world_frame.empty() && !casacore::MDirection::getType(frame, world_frame)) {
            return false;
        }
        const auto world_direction = [&](const casacore::Vector<casacore::Double>& pixel, casacore::MVDirection& value) {
            casacore::MDirection world;
            if (!direction.toWorld(world, pixel)) {
                return false;
            }
            if (world.getRef().getType() != frame) {
                world = casacore::MDirection::Convert(world, frame)();
            }
            value = world.getValue();
            return true;
        };
        casacore::MVDirection origin;
        if (!world_direction(center, origin)) {
            return false;
        }
        for (int axis = 0; axis < 2; ++axis) {
            double offsets[2][2];
            for (int end = 0; end < 2; ++end) {
                casacore::Vector<casacore::Double> pixel(center.copy());
                pixel(axis) += end == 0 ? -0.5 : 0.5;
                casacore::MVDirection world;
                if (!world_direction(pixel, world)) {
                    return false;
                }
                const double distance = origin.separation(world);
                const double angle = origin.positionAngle(world);
                offsets[end][0] = distance * std::sin(angle);
                offsets[end][1] = distance * std::cos(angle);
            }
            transform(0, axis) = offsets[1][0] - offsets[0][0];
            transform(1, axis) = offsets[1][1] - offsets[0][1];
        }
        unit = casacore::Unit("rad");
    } else {
        const auto units = coord_sys.worldAxisUnits();
        unit = casacore::Unit(units(0));
        for (int axis = 0; axis < 2; ++axis) {
            casacore::Vector<casacore::Double> start(center.copy()), end(center.copy());
            start(axis) -= 0.5;
            end(axis) += 0.5;
            casacore::Vector<casacore::Double> world_start, world_end;
            if (!coord_sys.toWorld(world_start, start) || !coord_sys.toWorld(world_end, end)) {
                return false;
            }
            for (int row = 0; row < 2; ++row) {
                transform(row, axis) = casacore::Quantity(world_end(row) - world_start(row), units(row)).get(unit).getValue();
            }
        }
    }
    return true;
}
} // namespace

bool PixelEllipseAxesToWorld(const casacore::CoordinateSystem& coord_sys, double first, double second, casacore::Quantity& rotation,
    std::vector<casacore::Quantity>& axes, const casacore::Vector<casacore::Double>* center) {
    if (center) {
        casacore::Matrix<casacore::Double> transform;
        casacore::Unit unit;
        double major, minor, angle;
        if (!PixelToWorldEllipseTransform(coord_sys, *center, "", transform, unit) ||
            !TransformEllipseAxes(first, second, rotation.get("deg").getValue(), transform, major, minor, angle)) {
            return false;
        }
        axes.push_back(casacore::Quantity(major, unit));
        axes.push_back(casacore::Quantity(minor, unit));
        rotation = casacore::Quantity(angle, "deg");
        return true;
    }
    // The restored first radius follows rotation, measured from the horizontal axis.
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
        double major, minor, major_angle;
        if (!TransformEllipseAxes(first, second, rotation.get("deg").getValue(), sx, sy, major, minor, major_angle)) {
            return false;
        }
        axes.push_back(casacore::Quantity(major, x_scale.getUnit()));
        axes.push_back(casacore::Quantity(minor, x_scale.getUnit()));
        rotation = casacore::Quantity(major_angle, "deg");
    }
    return true;
}

bool WorldEllipseAxesToPixels(const casacore::CoordinateSystem& coord_sys, const casacore::Vector<casacore::Double>& center,
    const casacore::Quantity& first, const casacore::Quantity& second, double angle_degrees, const std::string& world_frame,
    CARTA::Point& axes, double& rotation) {
    casacore::Matrix<casacore::Double> transform;
    casacore::Unit unit;
    if (!PixelToWorldEllipseTransform(coord_sys, center, world_frame, transform, unit)) {
        return false;
    }
    const double determinant = transform(0, 0) * transform(1, 1) - transform(0, 1) * transform(1, 0);
    if (!std::isfinite(determinant) || determinant == 0) {
        return false;
    }
    casacore::Matrix<casacore::Double> inverse(2, 2);
    inverse(0, 0) = transform(1, 1) / determinant;
    inverse(1, 1) = transform(0, 0) / determinant;
    inverse(0, 1) = -transform(0, 1) / determinant;
    inverse(1, 0) = -transform(1, 0) / determinant;
    double major, minor;
    if (!TransformEllipseAxes(first.get(unit).getValue(), second.get(unit).getValue(), angle_degrees, inverse, major, minor, rotation)) {
        return false;
    }
    axes.set_x(minor);
    axes.set_y(major);
    rotation = std::fmod(rotation + 360.0, 360.0);
    return true;
}

bool TransformEllipseAxes(double first, double second, double angle_degrees, double scale_x, double scale_y, double& major, double& minor,
    double& major_angle_degrees) {
    casacore::Matrix<casacore::Double> transform(2, 2, 0.0);
    transform(0, 0) = scale_x;
    transform(1, 1) = scale_y;
    return TransformEllipseAxes(first, second, angle_degrees, transform, major, minor, major_angle_degrees);
}

bool TransformEllipseAxes(double first, double second, double angle_degrees, const casacore::Matrix<casacore::Double>& transform,
    double& major, double& minor, double& major_angle_degrees) {
    if (first <= 0 || second <= 0 || transform.nrow() != 2 || transform.ncolumn() != 2) {
        return false;
    }

    const double angle = angle_degrees * M_PI / 180.0;
    const double c = std::cos(angle), s = std::sin(angle);
    const double major_x = first * c, major_y = first * s;
    const double minor_x = -second * s, minor_y = second * c;
    const double transformed_major_x = transform(0, 0) * major_x + transform(0, 1) * major_y;
    const double transformed_major_y = transform(1, 0) * major_x + transform(1, 1) * major_y;
    const double transformed_minor_x = transform(0, 0) * minor_x + transform(0, 1) * minor_y;
    const double transformed_minor_y = transform(1, 0) * minor_x + transform(1, 1) * minor_y;
    const double xx = transformed_major_x * transformed_major_x + transformed_minor_x * transformed_minor_x;
    const double yy = transformed_major_y * transformed_major_y + transformed_minor_y * transformed_minor_y;
    const double xy = transformed_major_x * transformed_major_y + transformed_minor_x * transformed_minor_y;
    if (!std::isfinite(xx + yy + xy) || xx + yy <= 0) {
        return false;
    }
    const double difference = std::hypot(xx - yy, 2 * xy);
    const bool circular = difference <= 1e-12 * (xx + yy);
    major = std::sqrt((xx + yy + (circular ? 0 : difference)) / 2);
    minor = std::sqrt(std::max(0.0, (xx + yy - (circular ? 0 : difference)) / 2));
    major_angle_degrees = circular ? 0 : std::atan2(2 * xy, xx - yy) * 90.0 / M_PI;
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
