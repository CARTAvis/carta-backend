/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# Ds9ImportExport.h: handle DS9 region file import and export

#ifndef CARTA_BACKEND_REGION_DS9IMPORTEXPORT_H_
#define CARTA_BACKEND_REGION_DS9IMPORTEXPORT_H_

#include <casacore/casa/Arrays/IPosition.h>
#include <casacore/coordinates/Coordinates/CoordinateSystem.h>

#include "Region.h"
#include "RegionImportExport.h"

namespace carta {

class Ds9ImportExport : public RegionImportExport {
public:
    // Import constructor
    // Parse input file and convert region parameters to RegionProperties for given image
    // file_is_filename : indicates whether file parameter contains file name or file contents.
    Ds9ImportExport(std::shared_ptr<casacore::CoordinateSystem> image_coord_sys, const casacore::IPosition& image_shape, int file_id,
        const std::string& file, bool file_is_filename);

    // Export constructor
    // Each export region will be converted to a string in DS9 format and added to string vector
    Ds9ImportExport(std::shared_ptr<casacore::CoordinateSystem> image_coord_sys, const casacore::IPosition& image_shape, bool pixel_coord);

    // Export regions
    // RegionState control points for pixel coords in reference image
    bool AddExportRegion(const RegionState& region_state, const CARTA::RegionStyle& region_style) override;

    // Print regions to file or vector
    bool ExportRegions(std::string& filename, std::string& error) override;
    bool ExportRegions(std::vector<std::string>& contents, std::string& error) override;

protected:
    // Add to type:name dictionary for DS9 syntax
    void AddRegionNames() override;

    bool AddExportRegion(CARTA::RegionType region_type, const std::vector<casacore::Quantity>& control_points,
        const casacore::Quantity& rotation, const CARTA::RegionStyle& style) override;

private:
    // Default global properties for export
    void InitGlobalProperties();

    // Parse each file line and set coord sys or region
    void ProcessFileLines(std::vector<std::string>& lines);

    // Coordinate system handlers
    void InitDs9CoordMap();
    bool IsDs9CoordSysKeyword(std::string& input_line);
    bool SetFileReferenceFrame(std::string& ds9_coord);
    void SetImageReferenceFrame();

    // Import regions
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

    // Export: add header string to _export_regions
    void AddHeader();
    std::string AddExportRegionPixel(CARTA::RegionType region_type, const std::vector<casacore::Quantity>& control_points, float angle,
        const CARTA::RegionStyle& region_style);
    std::string AddExportRegionWorld(CARTA::RegionType region_type, const std::vector<casacore::Quantity>& control_points, float angle,
        const CARTA::RegionStyle& region_style);
    void ExportStyleParameters(const CARTA::RegionStyle& region_style, std::string& region_line);
    void ExportTextboxStyleParameters(const CARTA::RegionStyle& region_style, std::string& region_line);
    void ExportFontParameters(const CARTA::RegionStyle& region_style, std::string& region_line);
    void ExportAnnotationStyleParameters(CARTA::RegionType region_type, const CARTA::RegionStyle& region_style, std::string& region_line);
    void ExportAnnPointParameters(const CARTA::RegionStyle& region_style, std::string& region_line);

    // DS9/CASA conversion map
    std::unordered_map<std::string, std::string> _coord_map;
    std::string _image_ref_frame; // CASA
    std::string _file_ref_frame;  // Import: DS9 to CASA, Export: CASA to DS9

    // Whether import region file is in pixel or wcs coords
    bool _pixel_coord;

    // Default properties
    std::unordered_map<std::string, std::string> _global_properties;
};

} // namespace carta

#endif // CARTA_BACKEND_REGION_DS9IMPORTEXPORT_H_
