/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# RegionImport.h: handle region import in CRTF and DS9 formats

#ifndef CARTA_SRC_REGION_REGIONIMPORTEXPORT_REGIONIMPORTER_H_
#define CARTA_SRC_REGION_REGIONIMPORTEXPORT_REGIONIMPORTER_H_

#include "Region/RegionState.h"
#include "RegionImportExportUtil.h"
#include "Util/Message.h"
#include "Util/String.h"

namespace carta {

/** @brief Data structure for returning imported region parameters and frontend styling. */
struct RegionProperties {
    /** @brief Default constructor. */
    RegionProperties() {}

    /** @brief Constructor to set region state and style. */
    RegionProperties(RegionState& region_state, CARTA::RegionStyle& region_style) : state(region_state), style(region_style) {}

    /** @brief Data structure holding region parameters. */
    RegionState state;
    /** @brief CARTA RegionStyle submessage holding region style parameters. */
    CARTA::RegionStyle style;
};

/** @brief Base class for importing regions from file. */
class RegionImporter {
public:
    /**
     * @brief Constructor for RegionImporter class to set image coordinate system and file id of reference image.
     * @param image_coord_sys casacore::CoordinateSystem of image from which region is exported
     * @param file_id File id of reference image to set RegionState in RegionProperties
     */
    RegionImporter(std::shared_ptr<casacore::CoordinateSystem> image_coord_sys, int file_id);

    /** @brief Default destructor */
    virtual ~RegionImporter() = default;

    /**
     * @brief Return regions in _import_regions and report any import errors.
     * @param[out] error Errors from any regions which failed to import
     * @return Imported regions as vector of RegionProperties struct
     */
    std::vector<RegionProperties> GetImportedRegions(std::string& error);

protected:
    /**
     * @brief Read region file into file lines.
     * @param file Filename to read or file contents as single string
     * @param file_is_filename Whether file is filename or contents
     * @param extra_delim Delimiter to be used by parser besides what is set by SetParserDelim
     * @return Region file lines
     */
    virtual std::vector<std::string> ReadRegionFile(const std::string& file, bool file_is_filename, const char extra_delim = '\0');

    /**
     * @brief Whether file line is a comment or an annotation region which starts with comment marker.
     * @param line File line to evaluate
     * @return Whether line is comment
     */
    virtual bool IsCommentLine(const std::string& line);

    /**
     * @brief Set parser delimiter to use in parsing file line
     * @param delim Delimiter string
     */
    virtual inline void SetParserDelim(const std::string& delim) {
        _parser_delim = delim;
    }

    /**
     * @brief Parse file line into region parameters and style properties
     * @param[in] region_definition File line to parse, determine to be a region
     * @param[out] parameters Strings parsed from file line describing region
     * @param[out] properties Map of values in file line after region definition describing style
     */
    virtual void ParseRegionParameters(
        std::string& region_definition, std::vector<std::string>& parameters, std::unordered_map<std::string, std::string>& properties);

    /**
     * @brief Return value of name in properties or _global_properties maps
     * @param name Keyword name of property in map
     * @param properties Map of values from file line after region definition
     * @param check_global Whether to check _global_properties set at beginning of region file for all regions
     * @return Map value for name, or empty string if name is not found
     */
    virtual std::string GetProperty(
        const std::string& name, const std::unordered_map<std::string, std::string>& properties, bool check_global = false);

    /**
     * @brief Convert text region string position to CARTA position enum
     * @param position Position string parsed from file line
     * @return CARTA::TextAnnotationPosition enum, or default CENTER if conversion failed
     */
    CARTA::TextAnnotationPosition GetTextPosition(const std::string& position);

    /**
     * @brief Add imported text region style, for text box, to existing textbox region properties (from two lines in region file).
     * @param[in] text_style CARTA RegionStyle submessage holding text region style parameters
     * @param[in, out] textbox_properties RegionProperties struct holding textbox properties
     */
    void AddTextStyleToProperties(const CARTA::RegionStyle& text_style, RegionProperties& textbox_properties);

    /**
     * @brief Convert ellipse radius, rotated box width/height, or compass length in world coordinates to pixel length.
     * @param input Value to convert as casacore Quantity
     * @param pixel_axis Index of axis for length conversion (axis unit and increment)
     * @return length
     */
    double WorldToPixelLength(casacore::Quantity input, unsigned int pixel_axis);

    /**
     * @brief Set CARTA AnnotationStyle parameters for compass region.
     * @param[in] compass_properties Map of values in file line after compass region definition
     * @param[in] coordinate_system String describing compass coordinate frame
     * @param[out] annotation_style CARTA AnnotationStyle submessage
     */
    void ImportCompassStyle(std::string& compass_properties, std::string& coordinate_system, CARTA::AnnotationStyle* annotation_style);

    /**
     * @brief Set coordinate system for ruler region.
     * @param[in] ruler_properties Map of values in file line after ruler region definition
     * @param[out] coordinate_system String describing ruler coordinate frame
     */
    void ImportRulerStyle(std::string& ruler_properties, std::string& coordinate_system);

    /** @brief Image coordinate system, from constructor. */
    std::shared_ptr<casacore::CoordinateSystem> _coord_sys;

    /** @brief File id of reference image, from constructor. */
    int _file_id;

    /** @brief Delimiter for parsing region file, specific to file type. */
    std::string _parser_delim;

    /** @brief Map of properties from parsed global line of region file. */
    std::unordered_map<std::string, std::string> _global_properties;

    /** @brief Region names for conversion CRtF or DS9 name to CARTA type. */
    std::unordered_map<CARTA::RegionType, std::string> _region_names;

    /** @brief Errors for region lines which failed to be imported. */
    std::string _import_errors;

    /** @brief Regions imported from region file. */
    std::vector<RegionProperties> _import_regions;
};

} // namespace carta

#endif // CARTA_SRC_REGION_REGIONIMPORTEXPORT_REGIONIMPORTER_H_
