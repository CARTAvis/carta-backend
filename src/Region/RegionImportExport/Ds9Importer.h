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
     * @param coord_sys casacore::CoordinateSystem of image from which region is imported
     * @param file_id Id of image from which region is imported
     * @param file File name or contents to import
     * @param file_is_filename Indicates whether file parameter contains name or contents.
     */
    Ds9Importer(std::shared_ptr<casacore::CoordinateSystem> coord_sys, int file_id, const std::string& file, bool file_is_filename);

private:
    /**
     * @brief Parse each line of file to import regions
     * @param file_lines Lines read from region file
     */
    void SetFileLineRegions(std::vector<std::string>& file_lines);

    /**
     * @brief Check if region file line is a DS9 coordinate.
     * @param file_line Line of region file to check
     */
    bool IsDs9Coord(std::string& file_line);

    /**
     * @brief Convert DS9 coordinate frame to casacore coordinate frame
     * @param ds9_coord File line with DS9 coordinate frame
     */
    bool SetFileCoordFrame(std::string& ds9_coord);

    /**
     * @brief Set properties from global file line
     * @param file_line File line with global values
     */
    void SetGlobals(std::string& file_line);

    /**
     * @brief Parse file line to set a region
     * @param file_line File line to parse
     * @return region properties struct
     */
    RegionProperties SetRegion(std::string& file_line);

    /**
     * @brief Import point or text region.
     * @param parameters Region definition parameters
     * @param is_annotation Whether region is annotation region
     * @return region state struct
     */
    RegionState ImportPointRegion(std::vector<std::string>& parameters, bool is_annotation = false);

    /**
     * @brief Import circle or compass region.
     * @param parameters Region definition parameters
     * @param is_annotation Whether region is annotation region
     * @return region state struct
     */
    RegionState ImportCircleRegion(std::vector<std::string>& parameters, bool is_annotation = false);

    /**
     * @brief Import ellipse, circle, or compass region.
     * @param parameters Region definition parameters
     * @param is_annotation Whether region is annotation region
     * @return region state struct
     */
    RegionState ImportEllipseRegion(std::vector<std::string>& parameters, bool is_annotation = false);

    /**
     * @brief Import rectangle region.
     * @param parameters Region definition parameters
     * @param is_annotation Whether region is annotation region
     * @return region state struct
     */
    RegionState ImportRectangleRegion(std::vector<std::string>& parameters, bool is_annotation = false);

    /**
     * @brief Import polygon and line-based regions.
     * @param parameters Region definition parameters
     * @param is_annotation Whether region is annotation region
     * @return region state struct
     */
    RegionState ImportPolygonLineRegion(std::vector<std::string>& parameters, bool is_annotation = false);

    /**
     * @brief Import vector region.
     * @param parameters Region definition parameters
     * @return region state struct
     */
    RegionState ImportVectorRegion(std::vector<std::string>& parameters);

    /**
     * @brief Import ruler region and its style parameters.
     * @param[in] parameters Region definition parameters
     * @param[in] properties Map of style parameters
     * @param[out] region_style Region style parameters
     * @return region state struct
     */
    RegionState ImportRulerRegion(
        std::vector<std::string>& parameters, std::unordered_map<std::string, std::string>& properties, CARTA::RegionStyle& region_style);

    /**
     * @brief Import compass region and its style parameters.
     * @param[in] parameters Region definition parameters
     * @param[in] properties Map of style parameters
     * @param[out] region_style Region style parameters
     * @return region state struct
     */
    RegionState ImportCompassRegion(
        std::vector<std::string>& parameters, std::unordered_map<std::string, std::string>& properties, CARTA::RegionStyle& region_style);

    /**
     * @brief Set common region style parameters.
     * @param region_type Region type
     * @param properties Map of style parameters
     * @return Region style parameters
     */
    CARTA::RegionStyle ImportStyle(CARTA::RegionType region_type, std::unordered_map<std::string, std::string>& properties);

    /**
     * @brief Set annotation style point parameters.
     * @param[in] properties Map of style parameters
     * @param[out] annotation_style Annotation style parameters
     */
    void ImportPointStyle(std::unordered_map<std::string, std::string>& properties, CARTA::AnnotationStyle* annotation_style);

    /**
     * @brief Set annotation style font parameters.
     * @param[in] properties Map of style parameters
     * @param[out] annotation_style Annotation style parameters
     */
    void ImportFontStyle(std::unordered_map<std::string, std::string>& properties, CARTA::AnnotationStyle* annotation_style);

    /**
     * @brief Convert region parameter string to casacore Quantity.
     * @param[in] parameter Region parameter string to convert
     * @param[in] is_angle Whether param describes an angle quantity
     * @param[in] is_xy Whether param describes a position quantity
     * @param[in] region_name Region name
     * @param[out] param_quantity Region parameter as casacore Quantity
     * @return Whether conversion is successful
     */
    bool ParameterToQuantity(
        std::string& parameter, bool is_angle, bool is_xy, std::string& region_name, casacore::Quantity& param_quantity);

    /**
     * @brief Convert DS9 unit to casacore unit in parameter
     * @param[in] region_name Region name
     * @param[in, out] parameter Region parameter string to convert
     * @return Whether conversion is successful
     */
    bool Ds9ToCasacoreUnit(const std::string& region_name, std::string& parameter);

    /**
     * @brief Convert parameter in sexagesimal format to angle format
     * @param[in, out] parameter Region parameter string to convert
     */
    void ConvertTimeFormatToAngle(std::string& parameter);

    /** @brief casacore coordinate frame from image coordinate system. */
    std::string _image_coord_frame;

    /** @brief DS9 coordinate frame converted to casacore, from region file. */
    std::string _file_coord_frame;

    // Whether to regions in file are in pixel or world coords
    bool _import_pixels;

    /** @brief DS9 to casacore coordinate frame. */
    static std::unordered_map<std::string, std::string> _coordinate_frames;
};

} // namespace carta

#endif // CARTA_SRC_REGION_REGIONIMPORTEXPORT_DS9IMPORTER_H_
