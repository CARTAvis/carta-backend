/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# CrtfImportExport.h: handle CRTF region file import and export

#ifndef CARTA_SRC_REGION_REGIONIMPORTEXPORT_CRTFIMPORT_H_
#define CARTA_SRC_REGION_REGIONIMPORTEXPORT_CRTFIMPORT_H_

#include "RegionImport.h"

#define REGION_DASH_LENGTH 2

namespace carta {

class CrtfImport : public RegionImport {
public:
    // file_is_filename : indicates whether file parameter contains file name or file contents.
    CrtfImport(std::shared_ptr<casacore::CoordinateSystem> image_coord_sys, int file_id, const std::string& file, bool file_is_filename);

private:
    void ProcessFileLines(std::vector<std::string>& lines);
    std::string GetRegionDirectionFrame(const std::unordered_map<std::string, std::string>& properties);
    RegionState ImportAnnSymbolText(std::vector<std::string>& parameters, std::string& coord_frame);
    RegionState ImportAnnBox(std::vector<std::string>& parameters, std::string& coord_frame);
    RegionState ImportAnnEllipse(std::vector<std::string>& parameters, std::string& coord_frame);
    RegionState ImportAnnPoly(std::vector<std::string>& parameters, std::string& coord_frame);
    CARTA::RegionStyle ImportStyleParameters(CARTA::RegionType region_type, const std::unordered_map<std::string, std::string>& properties);
    void ImportFontStyleParameters(
        const std::unordered_map<std::string, std::string>& properties, CARTA::AnnotationStyle* annotation_style);
    void ImportPointStyleParameters(const std::string& symbol_char, const std::unordered_map<std::string, std::string>& properties,
        CARTA::AnnotationStyle* annotation_style);

    // Rectangle import helpers
    bool GetBoxControlPoints(std::string& box_definition, std::vector<CARTA::Point>& control_points, float& rotation);
    bool GetBoxControlPoints(
        std::vector<std::string>& parameters, std::string& region_frame, std::vector<CARTA::Point>& control_points, float& rotation);
    bool GetCenterBoxPoints(const std::string& region, casacore::Quantity& cx, casacore::Quantity& cy, casacore::Quantity& width,
        casacore::Quantity& height, std::string& region_frame, std::vector<CARTA::Point>& control_points);
    bool GetRectBoxPoints(casacore::Quantity& blcx, casacore::Quantity& blcy, casacore::Quantity& trcx, casacore::Quantity& trcy,
        std::string& region_frame, std::vector<CARTA::Point>& control_points);
};

} // namespace carta

#endif // CARTA_SRC_REGION_REGIONIMPORTEXPORT_CRTFIMPORT_H_
