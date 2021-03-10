/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# RegionImportExport.h: handle region import/export in CRTF and DS9 formats

#ifndef CARTA_BACKEND_REGION_REGIONIMPORTEXPORT_H_
#define CARTA_BACKEND_REGION_REGIONIMPORTEXPORT_H_

#include <carta-protobuf/defs.pb.h>
#include <casacore/casa/Arrays/IPosition.h>
#include <casacore/coordinates/Coordinates/CoordinateSystem.h>

#include "RegionHandler.h"

namespace carta {

class RegionImportExport {
public:
    RegionImportExport() {}
    virtual ~RegionImportExport() {}

    // Import constructor: file_id to add to RegionState
    RegionImportExport(casacore::CoordinateSystem* image_coord_sys, const casacore::IPosition& image_shape, int file_id);
    // Export constructor
    RegionImportExport(casacore::CoordinateSystem* image_coord_sys, const casacore::IPosition& image_shape);

    // Retrieve imported regions as RegionState vector
    std::vector<RegionProperties> GetImportedRegions(std::string& error);

    // Add region to export: RegionState for pixel coords in reference image,
    // Quantities for world coordinates or for either coordinate type applied to another image
    virtual bool AddExportRegion(const RegionState& region_state, const RegionStyle& region_style) = 0;
    bool AddExportRegion(
        const RegionState& region_state, const RegionStyle& region_style, const casacore::RecordInterface& region_record, bool pixel_coord);

    // Perform export; ostream could be for output file (ofstream) or string (ostringstream)
    virtual bool ExportRegions(std::string& filename, std::string& error) = 0;
    virtual bool ExportRegions(std::vector<std::string>& contents, std::string& error) = 0;

protected:
    // Parse file into lines, return in string vector
    virtual std::vector<std::string> ReadRegionFile(const std::string& file, bool file_is_filename, const char extra_delim = '\0');

    // Parse file line into region name and parameters
    virtual inline void SetParserDelim(const std::string& delim) {
        _parser_delim = delim;
    }
    virtual void ParseRegionParameters(
        std::string& region_definition, std::vector<std::string>& parameters, std::unordered_map<std::string, std::string>& properties);

    virtual bool AddExportRegion(const RegionState& region_state, const RegionStyle& region_style,
        const std::vector<casacore::Quantity>& control_points, const casacore::Quantity& rotation) = 0;

    // Convert wcs -> pixel
    bool ConvertPointToPixels(
        std::string& region_frame, std::vector<casacore::Quantity>& point, casacore::Vector<casacore::Double>& pixel_coords);
    double WorldToPixelLength(casacore::Quantity world_length, unsigned int pixel_axis);

    // Format hex string e.g. "10161a" -> "#10161A"
    std::string FormatColor(const std::string& color);

    // Image info to which region is applied
    casacore::CoordinateSystem* _coord_sys;
    casacore::IPosition _image_shape;

    // For import
    int _file_id;
    std::string _parser_delim;
    std::string _import_errors;
    std::vector<RegionProperties> _import_regions;
    std::vector<std::string> _export_regions;

private:
    // Return control_points and qrotation Quantity for region type
    bool ConvertRecordToPoint(
        const casacore::RecordInterface& region_record, bool pixel_coord, std::vector<casacore::Quantity>& control_points);
    bool ConvertRecordToRectangle(
        const casacore::RecordInterface& region_record, bool pixel_coord, std::vector<casacore::Quantity>& control_points);
    bool ConvertRecordToEllipse(const RegionState& region_state, const casacore::RecordInterface& region_record, bool pixel_coord,
        std::vector<casacore::Quantity>& control_points, casacore::Quantity& qrotation);
    bool ConvertRecordToPolygon(
        const casacore::RecordInterface& region_record, bool pixel_coord, std::vector<casacore::Quantity>& control_points);
};

} // namespace carta

#endif // CARTA_BACKEND_REGION_REGIONIMPORTEXPORT_H_
