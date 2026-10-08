/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# RegionImportExportUtil.h: functions common to CRTF and DS9 import and export

#ifndef CARTA_SRC_REGION_REGIONIMPORTEXPORT_REGIONIMPORTEXPORTUTIL_H_
#define CARTA_SRC_REGION_REGIONIMPORTEXPORT_REGIONIMPORTEXPORTUTIL_H_

#include <casacore/coordinates/Coordinates/CoordinateSystem.h>

#include <carta-protobuf/defs.pb.h>

namespace carta {

/** @brief Map from CARTA text position to string. */
static std::unordered_map<CARTA::TextAnnotationPosition, std::string> text_positions{{CARTA::TextAnnotationPosition::CENTER, "center"},
    {CARTA::TextAnnotationPosition::UPPER_LEFT, "uleft"}, {CARTA::TextAnnotationPosition::UPPER_RIGHT, "uright"},
    {CARTA::TextAnnotationPosition::LOWER_LEFT, "lleft"}, {CARTA::TextAnnotationPosition::LOWER_RIGHT, "lright"},
    {CARTA::TextAnnotationPosition::TOP, "top"}, {CARTA::TextAnnotationPosition::BOTTOM, "bottom"},
    {CARTA::TextAnnotationPosition::LEFT, "left"}, {CARTA::TextAnnotationPosition::RIGHT, "right"}};

/** @brief Map from CARTA region type to string, common to all region file types. */
static std::unordered_map<CARTA::RegionType, std::string> region_names{{CARTA::RegionType::LINE, "line"},
    {CARTA::RegionType::POLYLINE, "polyline"}, {CARTA::RegionType::ELLIPSE, "ellipse"}, {CARTA::RegionType::ANNULUS, "ellipse"},
    {CARTA::RegionType::ANNRULER, "# ruler"}, {CARTA::RegionType::ANNCOMPASS, "# compass"}};

/**
 * @brief Return map from CARTA region type to string for region file type.
 * @param region_file_type CARTA FileType enum designating CRTF or DS9 region file
 * @return region name map
 */
std::unordered_map<CARTA::RegionType, std::string> GetRegionTypeNames(CARTA::FileType region_file_type);

/** @brief Convert pixel ellipse radii and their angle from the horizontal axis to world geometry. */
bool PixelEllipseAxesToWorld(const casacore::CoordinateSystem& coord_sys, double first, double second, casacore::Quantity& rotation,
    std::vector<casacore::Quantity>& axes, const casacore::Vector<casacore::Double>* center = nullptr);

/** @brief Convert world ellipse axes into pixel radii and CARTA rotation using the local WCS in the requested frame. */
bool WorldEllipseAxesToPixels(const casacore::CoordinateSystem& coord_sys, const casacore::Vector<casacore::Double>& center,
    const casacore::Quantity& first, const casacore::Quantity& second, double angle_degrees, const std::string& world_frame,
    CARTA::Point& axes, double& rotation);

/** @brief Transform ellipse radii by axis-aligned scales and return principal radii and angle. */
bool TransformEllipseAxes(double first, double second, double angle_degrees, double scale_x, double scale_y, double& major, double& minor,
    double& major_angle_degrees);

/** @brief Transform ellipse radii by a complete two-dimensional pixel transform. */
bool TransformEllipseAxes(double first, double second, double angle_degrees, const casacore::Matrix<casacore::Double>& transform,
    double& major, double& minor, double& major_angle_degrees);

/** @brief Check whether two positive region radii describe a circle. */
bool IsApproximatelyCircular(double radius_x, double radius_y);

/**
 * @brief Return name of coordinate frame from coordinate system direction coordinate, if any.
 * @param coord_sys casacore::CoordinateSystem of image to use for frame
 * @return direction frame
 */
std::string GetImageDirectionFrame(std::shared_ptr<casacore::CoordinateSystem> coord_sys);

/**
 * @brief Convert point control points in world coordinates to pixel coordinates in region frame.
 * @param[in] coord_sys casacore CoordinateSystem describing world coordinates used for point
 * @param[in] region_frame Coordinate frame to convert world coordinates for region pixels
 * @param[in] point Point region world control points as casacore Quantities
 * @param[out] pixel_coords Point region pixel control points as double values
 * @return Whether conversion is successful
 */
bool ConvertPointToPixels(std::shared_ptr<casacore::CoordinateSystem> coord_sys, std::string& region_frame,
    std::vector<casacore::Quantity>& point, casacore::Vector<casacore::Double>& pixel_coords);

/**
 * @brief Format input color string as expected for import or export.
 * @param color Color string
 * @return Formatted color string
 */
std::string FormatColor(const std::string& color);

} // namespace carta

#endif // CARTA_SRC_REGION_REGIONIMPORTEXPORT_REGIONIMPORTEXPORTUTIL_H_
