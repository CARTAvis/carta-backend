/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# Ds9Importer.h: handle DS9 region file import

#ifndef CARTA_SRC_REGION_REGIONIMPORTEXPORT_DS9IMPORTER_H_
#define CARTA_SRC_REGION_REGIONIMPORTEXPORT_DS9IMPORTER_H_

#include "RegionImporter.h"

namespace carta {

/** @brief A class for importing region files in DS9 format. */
class Ds9Importer : public RegionImporter {
public:
    /**
     * @brief Constructor for Ds9Importer class for importing regions in DS9 region file.
     * @param image_coord_sys casacore::CoordinateSystem of image from which region is exported
     * @param file_id Id of image to which region is imported, for RegionState
     * @param file File name or contents to import
     * @param file_is_filename Indicates whether file parameter contains name or contents.
     */
    Ds9Importer(std::shared_ptr<casacore::CoordinateSystem> image_coord_sys, int file_id, const std::string& file, bool file_is_filename);

private:
    /**
     * @brief Parse each line of file to add regions to _import_regions.
     * @param lines Lines read from region file
     */
    void ProcessFileLines(std::vector<std::string>& lines);

    /**
     * @brief Create _coord_map to convert DS9 coordinate in region file to casacore coordinate frame.
     */
    void InitDs9CoordMap();

    /**
     * @brief Check if region file line is DS9 coordinate in _coord_map.
     * @param input_line Line of region file identified as DS9 coordinate
     */
    bool IsDs9CoordSysKeyword(std::string& input_line);

    /**
     * @brief Convert DS9 coordinate frame to casacore coordinate frame and set _file_ref_frame.
     * @param ds9_coord DS9 coordinate frame
     */
    bool SetFileReferenceFrame(std::string& ds9_coord);

    /**
     * @brief Set image coordinate system direction frame in _image_ref_frame.
     */
    void SetImageReferenceFrame();

    /**
     * @brief Set _global_properties from region file line
     * @param global_line Line of region file identified as global values
     */
    void SetGlobals(std::string& global_line);

    /**
     * @brief Parse DS9 region definition to set RegionProperties.
     * @param region_definition File line defining a region and its style
     * @return RegionProperties struct
     */
    RegionProperties SetRegion(std::string& region_definition);

    /**
     * @brief Set RegionState for point region from parsed region parameters.
     * @param parameters Strings parsed from file line describing region
     * @param is_annotation Whether region is annotation region
     * @return RegionState struct
     */
    RegionState ImportPointRegion(std::vector<std::string>& parameters, bool is_annotation = false);

    /**
     * @brief Set RegionState for circle region from parsed region parameters.
     * @param parameters Strings parsed from file line describing region
     * @param is_annotation Whether region is annotation region
     * @return RegionState struct
     */
    RegionState ImportCircleRegion(std::vector<std::string>& parameters, bool is_annotation = false);

    /**
     * @brief Set RegionState for ellipse region from parsed region parameters.
     * @param parameters Strings parsed from file line describing region
     * @param is_annotation Whether region is annotation region
     * @return RegionState struct
     */
    RegionState ImportEllipseRegion(std::vector<std::string>& parameters, bool is_annotation = false);

    /**
     * @brief Set RegionState for rectangle region from parsed region parameters.
     * @param parameters Strings parsed from file line describing region
     * @param is_annotation Whether region is annotation region
     * @return RegionState struct
     */
    RegionState ImportRectangleRegion(std::vector<std::string>& parameters, bool is_annotation = false);

    /**
     * @brief Set RegionState for polygon or polyline region from parsed region parameters.
     * @param parameters Strings parsed from file line describing region
     * @param is_annotation Whether region is annotation region
     * @return RegionState struct
     */
    RegionState ImportPolygonLineRegion(std::vector<std::string>& parameters, bool is_annotation = false);

    /**
     * @brief Set RegionState for vector region from parsed region parameters.
     * @param parameters Strings parsed from file line describing region
     * @return RegionState struct
     */
    RegionState ImportVectorRegion(std::vector<std::string>& parameters);

    /**
     * @brief Set RegionState and RegionStyle for ruler region from parsed region parameters and properties.
     * @param[in] parameters Strings parsed from file line describing region
     * @param[in] properties Map of values in file line describing style
     * @param[out] CARTA RegionStyle submessage defining region style
     * @return RegionState struct
     */
    RegionState ImportRulerRegion(
        std::vector<std::string>& parameters, std::unordered_map<std::string, std::string>& properties, CARTA::RegionStyle& region_style);

    /**
     * @brief Set RegionState and RegionStyle for compass region from parsed region parameters and properties.
     * @param[in] parameters Strings parsed from file line describing region
     * @param[in] properties Map of values in file line describing style
     * @param[out] CARTA RegionStyle submessage defining region style
     * @return RegionState struct
     */
    RegionState ImportCompassRegion(
        std::vector<std::string>& parameters, std::unordered_map<std::string, std::string>& properties, CARTA::RegionStyle& region_style);

    /**
     * @brief Set RegionStyle from parsed region properties.
     * @param region_type CARTA RegionType enum defining type of region
     * @param properties Map of values in file line describing style
     * @return CARTA RegionStyle submessage defining region style
     */
    CARTA::RegionStyle ImportStyleParameters(CARTA::RegionType region_type, std::unordered_map<std::string, std::string>& properties);

    /**
     * @brief Set CARTA AnnotationStyle submessage parameters from properties for point region.
     * @param[in] properties Map of values in file line describing style
     * @param[out] annotation_style CARTA AnnotationStyle submessage
     */
    void ImportPointStyleParameters(std::unordered_map<std::string, std::string>& properties, CARTA::AnnotationStyle* annotation_style);

    /**
     * @brief Set CARTA AnnotationStyle submessage font parameters from properties.
     * @param[in] properties Map of values in file line describing style
     * @param[out] annotation_style CARTA AnnotationStyle submessage
     */
    void ImportFontStyleParameters(std::unordered_map<std::string, std::string>& properties, CARTA::AnnotationStyle* annotation_style);

    /**
     * @brief Convert region parameter string to casacore Quantity.
     * @param[in] param Region parameter string (value and unit) parsed from file line
     * @param[in] is_angle Whether param describes an angle quantity
     * @param[in] is_xy Whether param describes a position quantity
     * @param[in] region_name Region name parsed from file line
     * @param[out] param_quantity Region parameter as casacore Quantity
     * @return Whether conversion is successful
     */
    bool ParamToQuantity(std::string& param, bool is_angle, bool is_xy, std::string& region_name, casacore::Quantity& param_quantity);

    /**
     * @brief Convert DS9 unit to casacore unit in region parameter
     * @param[in] param Region parameter string (value and unit) parsed from file line
     * @param[in] region_name Region name parsed from file line
     * @return Whether conversion is successful
     */
    bool Ds9ToCasacoreUnit(std::string& param, const std::string& region_name);

    /**
     * @brief Convert parameter in sexagesimal format to angle format for casacore readQuantity
     * @param[in, out] param Region parameter string parsed from file line
     */
    void ConvertTimeFormatToAngle(std::string& parameter);

    /** @brief Conversion map from DS9 to casacore coordinate frame. */
    std::unordered_map<std::string, std::string> _coord_map;

    /** @brief casacore coordinate frame from image coordinate system. */
    std::string _image_ref_frame;

    /** @brief DS9 coordinate frame converted to casacore, from region file. */
    std::string _file_ref_frame;

    // Whether import region file is in pixel or wcs coords
    bool _pixel_coord;
};

} // namespace carta

#endif // CARTA_SRC_REGION_REGIONIMPORTEXPORT_DS9IMPORTER_H_
