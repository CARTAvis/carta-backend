/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# Ds9Import.h: handle DS9 region file import

#ifndef CARTA_SRC_REGION_REGIONIMPORTEXPORT_DS9IMPORT_H_
#define CARTA_SRC_REGION_REGIONIMPORTEXPORT_DS9IMPORT_H_

#include "RegionImport.h"

namespace carta {

class Ds9Import : public RegionImport {
public:
    Ds9Import(std::shared_ptr<casacore::CoordinateSystem> image_coord_sys, int file_id, const std::string& file, bool file_is_filename);

private:
    // Parse each file line and set coord sys or region
    void ProcessFileLines(std::vector<std::string>& lines);

    // Coordinate system handlers
    void InitDs9CoordMap();
    bool IsDs9CoordSysKeyword(std::string& input_line);
    bool SetFileReferenceFrame(std::string& ds9_coord);
    void SetImageReferenceFrame();

    void SetGlobals(std::string& global_line);
    RegionProperties SetRegion(std::string& region_definition);
    RegionState ImportPointRegion(std::vector<std::string>& parameters, bool is_annotation = false);
    RegionState ImportCircleRegion(std::vector<std::string>& parameters, bool is_annotation = false);
    RegionState ImportEllipseRegion(std::vector<std::string>& parameters, bool is_annotation = false);
    RegionState ImportRectangleRegion(std::vector<std::string>& parameters, bool is_annotation = false);
    RegionState ImportPolygonLineRegion(std::vector<std::string>& parameters, bool is_annotation = false);
    RegionState ImportVectorRegion(std::vector<std::string>& parameters);
    RegionState ImportRulerRegion(
        std::vector<std::string>& parameters, std::unordered_map<std::string, std::string>& properties, CARTA::RegionStyle& region_style);
    RegionState ImportCompassRegion(
        std::vector<std::string>& parameters, std::unordered_map<std::string, std::string>& properties, CARTA::RegionStyle& region_style);
    CARTA::RegionStyle ImportStyleParameters(CARTA::RegionType region_type, std::unordered_map<std::string, std::string>& properties);
    void ImportPointStyleParameters(std::unordered_map<std::string, std::string>& properties, CARTA::AnnotationStyle* annotation_style);
    void ImportFontStyleParameters(std::unordered_map<std::string, std::string>& properties, CARTA::AnnotationStyle* annotation_style);

    // Convert DS9 syntax -> CASA to read casacore::Quantity
    bool ParamToQuantity(std::string& param, bool is_angle, bool is_xy, std::string& region_name, casacore::Quantity& param_quantity);
    bool Ds9ToCasacoreUnit(std::string& parameter, const std::string& region_type);
    void ConvertTimeFormatToAngle(std::string& parameter);

    // DS9/CASA conversion map
    std::unordered_map<std::string, std::string> _coord_map;
    std::string _image_ref_frame; // CASA
    std::string _file_ref_frame;  // DS9 to CASA

    // Whether import region file is in pixel or wcs coords
    bool _pixel_coord;
};

} // namespace carta

#endif // CARTA_SRC_REGION_REGIONIMPORTEXPORT_DS9IMPORT_H_
