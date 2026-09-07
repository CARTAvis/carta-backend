/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# RegionImportExportUtil.h: functions common to CRTF and DS9 import and export

#ifndef CARTA_SRC_REGION_REGIONIMPORTEXPORT_REGIONIMPORTEXPORTUTIL_H_
#define CARTA_SRC_REGION_REGIONIMPORTEXPORT_REGIONIMPORTEXPORTUTIL_H_

#include <casacore/coordinates/Coordinates/CoordinateSystem.h>

#include <carta-protobuf/enums.pb.h>

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
