/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018 - 2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
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
    bool AddExportRegion(const RegionState& region_state, const RegionStyle& region_style) override;

    // Print regions to file or vector
    bool ExportRegions(std::string& filename, std::string& error) override;
    bool ExportRegions(std::vector<std::string>& contents, std::string& error) override;

protected:
    bool AddExportRegion(CARTA::RegionType region_type, const RegionStyle& style, const std::vector<casacore::Quantity>& control_points,
        const casacore::Quantity& rotation) override;

private:
    // Default global properties
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
    void SetRegion(std::string& region_definition);
    RegionState ImportPointRegion(std::vector<std::string>& parameters);
    RegionState ImportCircleRegion(std::vector<std::string>& parameters);
    RegionState ImportEllipseRegion(std::vector<std::string>& parameters);
    RegionState ImportRectangleRegion(std::vector<std::string>& parameters);
    RegionState ImportPolygonLineRegion(std::vector<std::string>& parameters);
    RegionStyle ImportStyleParameters(std::unordered_map<std::string, std::string>& properties);

    // Convert DS9 syntax -> CASA
    bool CheckAndConvertParameter(std::string& parameter, const std::string& region_type);
    void ConvertTimeFormatToDeg(std::string& parameter);

    // Export: add header string to _export_regions
    void AddHeader();
    std::string AddExportRegionPixel(CARTA::RegionType type, const std::vector<casacore::Quantity>& control_points, float angle);
    std::string AddExportRegionWorld(CARTA::RegionType type, const std::vector<casacore::Quantity>& control_points, float angle);
    void AddExportStyleParameters(const RegionStyle& region_style, std::string& region_line);

    // DS9/CASA conversion map
    std::unordered_map<std::string, std::string> _coord_map;
    std::string _image_ref_frame; // CASA
    std::string _file_ref_frame;  // Import: DS9 to CASA, Export: CASA to DS9

    // Whether import region file is in pixel or wcs coords
    bool _pixel_coord;

    // Default properties
    std::map<std::string, std::string> _global_properties;
};

} // namespace carta

#endif // CARTA_BACKEND_REGION_DS9IMPORTEXPORT_H_
