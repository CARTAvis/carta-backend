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

static std::unordered_map<CARTA::TextAnnotationPosition, std::string> text_positions{{CARTA::TextAnnotationPosition::CENTER, "center"},
    {CARTA::TextAnnotationPosition::UPPER_LEFT, "uleft"}, {CARTA::TextAnnotationPosition::UPPER_RIGHT, "uright"},
    {CARTA::TextAnnotationPosition::LOWER_LEFT, "lleft"}, {CARTA::TextAnnotationPosition::LOWER_RIGHT, "lright"},
    {CARTA::TextAnnotationPosition::TOP, "top"}, {CARTA::TextAnnotationPosition::BOTTOM, "bottom"},
    {CARTA::TextAnnotationPosition::LEFT, "left"}, {CARTA::TextAnnotationPosition::RIGHT, "right"}};

static std::unordered_map<CARTA::RegionType, std::string> region_names{{CARTA::RegionType::LINE, "line"},
    {CARTA::RegionType::POLYLINE, "polyline"}, {CARTA::RegionType::ELLIPSE, "ellipse"}, {CARTA::RegionType::ANNRULER, "# ruler"},
    {CARTA::RegionType::ANNCOMPASS, "# compass"}};

std::unordered_map<CARTA::RegionType, std::string> GetRegionTypeNames(CARTA::FileType region_file_type);

std::string GetImageDirectionFrame(std::shared_ptr<casacore::CoordinateSystem> coord_sys);

bool ConvertPointToPixels(std::shared_ptr<casacore::CoordinateSystem> coord_sys, std::string& region_frame,
    std::vector<casacore::Quantity>& point, casacore::Vector<casacore::Double>& pixel_coords);

std::string FormatColor(const std::string& color);

} // namespace carta

#endif // CARTA_SRC_REGION_REGIONIMPORTEXPORT_REGIONIMPORTEXPORTUTIL_H_
