/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
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

// CARTA default region style
#define REGION_COLOR "#2EE6D6"
#define REGION_DASH_LENGTH 2
#define REGION_LINE_WIDTH 2

namespace carta {

class RegionImportExport {
public:
    // Import constructor: file_id to add to RegionState
    RegionImportExport(std::shared_ptr<casacore::CoordinateSystem> image_coord_sys, const casacore::IPosition& image_shape, int file_id);
    // Export constructor
    RegionImportExport(std::shared_ptr<casacore::CoordinateSystem> image_coord_sys, const casacore::IPosition& image_shape);

    virtual ~RegionImportExport() = default;

    // Retrieve imported regions as RegionState vector
    std::vector<RegionProperties> GetImportedRegions(std::string& error);

    // Add region to export using RegionState for reference image or casacore::Record for matched region
    virtual bool AddExportRegion(const RegionState& region_state, const CARTA::RegionStyle& region_style) = 0;
    bool AddExportRegion(const RegionState& region_state, const CARTA::RegionStyle& region_style,
        const casacore::RecordInterface& region_record, bool pixel_coord);

    // Perform export; ostream could be for output file (ofstream) or string (ostringstream)
    virtual bool ExportRegions(std::string& filename, std::string& error) = 0;
    virtual bool ExportRegions(std::vector<std::string>& contents, std::string& error) = 0;

protected:
    // Add to type:name dictionary for CRTF/DS9
    virtual void AddExportRegionNames() = 0;

    // Parse file into lines, return in string vector
    virtual std::vector<std::string> ReadRegionFile(const std::string& file, bool file_is_filename, const char extra_delim = '\0');

    // Parse file line into region name and parameters
    virtual inline void SetParserDelim(const std::string& delim) {
        _parser_delim = delim;
    }
    virtual void ParseRegionParameters(
        std::string& region_definition, std::vector<std::string>& parameters, std::unordered_map<std::string, std::string>& properties);

    // Quantities for converted control points
    virtual bool AddExportRegion(CARTA::RegionType region_type, const std::vector<casacore::Quantity>& control_points,
        const casacore::Quantity& rotation, const CARTA::RegionStyle& region_style) = 0;

    // Convert wcs -> pixel
    bool ConvertPointToPixels(
        std::string& region_frame, std::vector<casacore::Quantity>& point, casacore::Vector<casacore::Double>& pixel_coords);
    double WorldToPixelLength(casacore::Quantity input, unsigned int pixel_axis);

    // Format hex string e.g. "10161a" -> "#10161A"
    std::string FormatColor(const std::string& color);
    void ExportAnnCompassStyle(const CARTA::RegionStyle& region_style, const std::string& ann_coord_sys, std::string& region_line);

    // Image info to which region is applied
    std::shared_ptr<casacore::CoordinateSystem> _coord_sys;
    casacore::IPosition _image_shape;

    // For import
    int _file_id;
    std::string _parser_delim;
    std::string _import_errors;
    std::vector<RegionProperties> _import_regions;
    std::vector<std::string> _export_regions;

    // Common to CRTF and DS9
    std::unordered_map<CARTA::RegionType, std::string> _region_names = {{CARTA::RegionType::LINE, "line"},
        {CARTA::RegionType::POLYLINE, "polyline"}, {CARTA::RegionType::ELLIPSE, "ellipse"}, {CARTA::RegionType::ANNRULER, "# ruler"},
        {CARTA::RegionType::ANNCOMPASS, "# compass"}};

private:
    // Return control_points and rotation Quantity for region type
    bool ConvertRecordToPoint(
        const casacore::RecordInterface& region_record, bool pixel_coord, std::vector<casacore::Quantity>& control_points);
    bool ConvertRecordToRectangle(
        const casacore::RecordInterface& region_record, bool pixel_coord, std::vector<casacore::Quantity>& control_points);
    bool ConvertRecordToEllipse(const RegionState& region_state, const casacore::RecordInterface& region_record, bool pixel_coord,
        std::vector<casacore::Quantity>& control_points, casacore::Quantity& qrotation);
    bool ConvertRecordToPolygonLine(
        const casacore::RecordInterface& region_record, bool pixel_coord, std::vector<casacore::Quantity>& control_points);
};

} // namespace carta

#endif // CARTA_BACKEND_REGION_REGIONIMPORTEXPORT_H_
