/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# RegionImport.h: handle region import in CRTF and DS9 formats

#ifndef CARTA_SRC_REGION_REGIONIMPORTEXPORT_REGIONIMPORT_H_
#define CARTA_SRC_REGION_REGIONIMPORTEXPORT_REGIONIMPORT_H_

#include "Region/RegionState.h"
#include "RegionImportExportUtil.h"
#include "Util/Message.h"
#include "Util/String.h"

namespace carta {

struct RegionProperties {
    RegionProperties() {}
    RegionProperties(RegionState& region_state, CARTA::RegionStyle& region_style) : state(region_state), style(region_style) {}

    RegionState state;
    CARTA::RegionStyle style;
};

class RegionImport {
public:
    // Import constructor: file_id to add to RegionState
    RegionImport(std::shared_ptr<casacore::CoordinateSystem> image_coord_sys, int file_id);

    virtual ~RegionImport() = default;

    // Retrieve imported regions
    std::vector<RegionProperties> GetImportedRegions(std::string& error);

protected:
    // Parse file into lines, return in string vector
    virtual std::vector<std::string> ReadRegionFile(const std::string& file, bool file_is_filename, const char extra_delim = '\0');

    // Some commented lines are carta regions
    virtual bool IsCommentLine(const std::string& line);

    // Parse file line into region name and parameters
    virtual inline void SetParserDelim(const std::string& delim) {
        _parser_delim = delim;
    }
    virtual void ParseRegionParameters(
        std::string& region_definition, std::vector<std::string>& parameters, std::unordered_map<std::string, std::string>& properties);

    virtual std::string GetProperty(
        const std::string& name, const std::unordered_map<std::string, std::string>& properties, bool check_global = false);

    // Handle combo region (textbox + text)
    CARTA::TextAnnotationPosition GetTextPosition(const std::string& position);
    void AddTextStyleToProperties(const CARTA::RegionStyle& text_style, RegionProperties& textbox_properties);

    // Convert wcs -> pixel
    double WorldToPixelLength(casacore::Quantity input, unsigned int pixel_axis);

    void ImportCompassStyle(std::string& compass_properties, std::string& coordinate_system, CARTA::AnnotationStyle* annotation_style);
    void ImportRulerStyle(std::string& ruler_properties, std::string& coordinate_system);

    // Image info to which region is applied
    std::shared_ptr<casacore::CoordinateSystem> _coord_sys;

    int _file_id;
    std::string _parser_delim;
    std::unordered_map<std::string, std::string> _global_properties;
    std::unordered_map<CARTA::RegionType, std::string> _region_names;

    std::string _import_errors;
    std::vector<RegionProperties> _import_regions;
};

} // namespace carta

#endif // CARTA_SRC_REGION_REGIONIMPORTEXPORT_REGIONIMPORT_H_
