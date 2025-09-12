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

/** @brief Data structure for returning imported region definition and style parameters. */
struct RegionProperties {
    /** @brief Default constructor. */
    RegionProperties() {}

    /** @brief Constructor to set region state and style. */
    RegionProperties(RegionState& region_state, CARTA::RegionStyle& region_style) : state(region_state), style(region_style) {}

    /** @brief Data structure holding region definition parameters. */
    RegionState state;
    /** @brief CARTA RegionStyle submessage holding region style parameters. */
    CARTA::RegionStyle style;
};

/** @brief Base class for importing regions from file. */
class RegionImporter {
public:
    /**
     * @brief Constructor for RegionImporter class
     * @param coord_sys Coordinate system of image to which regions are exported
     * @param file_id File id of image to which regions are imported
     */
    RegionImporter(std::shared_ptr<casacore::CoordinateSystem> coord_sys, int file_id);

    /** @brief Default destructor */
    virtual ~RegionImporter() = default;

    /**
     * @brief Return imported regions and report errors.
     * @param[out] error Errors from any regions which failed to import
     * @return regions as region properties struct
     */
    std::vector<RegionProperties> GetRegions(std::string& error);

protected:
    /**
     * @brief Read region file into file lines.
     * @param file Filename to read or file contents as single string
     * @param file_is_filename Whether file is filename or contents
     * @param extra_delim Delimiter to be used by parser besides what is set by SetParserDelim
     * @return file lines
     */
    virtual std::vector<std::string> ReadRegionFile(const std::string& file, bool file_is_filename, const char extra_delim = '\0');

    /**
     * @brief Determine whether file line starting with comment marker is a comment and not an annotation region.
     * @param file_line File line to evaluate
     * @return Whether line is comment
     */
    virtual bool IsCommentLine(const std::string& file_line);

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
     * @param[out] parameters Region definition parameters
     * @param[out] properties Map of style parameters
     */
    virtual void ParseRegionParameters(
        std::string& region_definition, std::vector<std::string>& parameters, std::unordered_map<std::string, std::string>& properties);

    /**
     * @brief Return value of name in region properties or global properties
     * @param name Name of property to get
     * @param properties Map of style parameters
     * @param check_global Whether to check global properties
     * @return Map value for name, or empty string if name is not found
     */
    virtual std::string GetProperty(
        const std::string& name, const std::unordered_map<std::string, std::string>& properties, bool check_global = false);

    /**
     * @brief Convert text region position to CARTA position enum
     * @param position Position in file line
     * @return CARTA text position, or default
     */
    CARTA::TextAnnotationPosition GetTextPosition(const std::string& position);

    /**
     * @brief Add text region style to textbox region properties
     * @param[in] text_style Region style parameters for text region
     * @param[in, out] textbox_properties Struct holding region properties for textbox region
     */
    void AddTextStyleToProperties(const CARTA::RegionStyle& text_style, RegionProperties& textbox_properties);

    /**
     * @brief Convert length region parameter to pixels if in world coordinates.
     * @param length Length quantity
     * @param pixel_axis Index of axis for conversion
     * @return length in pixels
     */
    double WorldToPixelLength(casacore::Quantity length, unsigned int pixel_axis);

    /**
     * @brief Set style parameters for compass region.
     * @param[in] compass_properties Portion of file line with style parameters
     * @param[out] compass_coord Compass coordinate frame
     * @param[out] annotation_style Compass style parameters
     */
    void ImportCompassStyle(std::string& compass_properties, std::string& compass_coord, CARTA::AnnotationStyle* annotation_style);

    /**
     * @brief Get style parameters for ruler region.
     * @param[in] ruler_properties Portion of file line with style parameters
     * @param[out] ruler_coord Ruler coordinate frame
     */
    void ImportRulerStyle(std::string& ruler_properties, std::string& ruler_coord);

    /** @brief Image coordinate system. */
    std::shared_ptr<casacore::CoordinateSystem> _coord_sys;

    /** @brief File id of reference image. */
    int _file_id;

    /** @brief Delimiter for parsing region file, specific to file type. */
    std::string _parser_delim;

    /** @brief Map of properties from global line of region file. */
    std::unordered_map<std::string, std::string> _global_properties;

    /** @brief Region names for conversion CRTF or DS9 name to CARTA type. */
    std::unordered_map<CARTA::RegionType, std::string> _region_names;

    /** @brief Errors for region lines which failed. */
    std::string _errors;

    /** @brief Imported regions. */
    std::vector<RegionProperties> _regions;
};

} // namespace carta

#endif // CARTA_SRC_REGION_REGIONIMPORTEXPORT_REGIONIMPORTER_H_
