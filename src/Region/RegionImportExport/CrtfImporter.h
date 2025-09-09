/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# CrtfImporter.h: handle CRTF region file import and export

#ifndef CARTA_SRC_REGION_REGIONIMPORTEXPORT_CRTFIMPORTER_H_
#define CARTA_SRC_REGION_REGIONIMPORTEXPORT_CRTFIMPORTER_H_

#include "RegionImporter.h"

#define REGION_DASH_LENGTH 2

namespace carta {

/** @brief A class for importing region files in CRTF format. */
class CrtfImporter : public RegionImporter {
public:
    /**
     * @brief Constructor for CrtfImporter class for importing regions in CRTF region file.
     * @param image_coord_sys casacore::CoordinateSystem of image from which region is exported
     * @param file_id Id of image to which region is imported, for RegionState
     * @param file File name or contents to import
     * @param file_is_filename Indicates whether file parameter contains name or contents.
     */
    CrtfImporter(std::shared_ptr<casacore::CoordinateSystem> image_coord_sys, int file_id, const std::string& file, bool file_is_filename);

private:
    /**
     * @brief Parse each line of file to add regions to _import_regions.
     * @param lines Lines read from region file
     */
    void ProcessFileLines(std::vector<std::string>& lines);

    /**
     * @brief Get direction frame coord value in region file properties, else use frame in coord sys.
     * @param properties Map of values in file line after region definition
     * @return name of direction frame
     */
    std::string GetRegionDirectionFrame(const std::unordered_map<std::string, std::string>& properties);

    /**
     * @brief Import point (possibly annotation) or text from region parameters.
     * @param parameters Strings parsed from file line describing region
     * @param coord_frame Direction frame used to define parameter points
     * @return region_state RegionState struct defining region parameters
     */
    RegionState ImportAnnSymbolText(std::vector<std::string>& parameters, std::string& coord_frame);

    /**
     * @brief Import box region (possibly annotation) from region parameters.
     * @param parameters Strings parsed from file line describing region
     * @param coord_frame Direction frame used to define parameter points
     * @return region_state RegionState struct defining region parameters
     */
    RegionState ImportAnnBox(std::vector<std::string>& parameters, std::string& coord_frame);

    /**
     * @brief Import ellipse region (possibly annotation) from region parameters.
     * @param parameters Strings parsed from file line describing region
     * @param coord_frame Direction frame used to define parameter points
     * @return region_state RegionState struct defining region parameters
     */
    RegionState ImportAnnEllipse(std::vector<std::string>& parameters, std::string& coord_frame);

    /**
     * @brief Import polygon or polyline region (possibly annotation) from region parameters.
     * @param parameters Strings parsed from file line describing region
     * @param coord_frame Direction frame used to define parameter points
     * @return region_state RegionState struct defining region parameters
     */
    RegionState ImportAnnPoly(std::vector<std::string>& parameters, std::string& coord_frame);

    /**
     * @brief Import CARTA RegionStyle parameters for region type from properties.
     * @param region_type CARTA RegionType enum defining type of region
     * @param properties Map of values in file line after region definition
     * @return CARTA RegionStyle submessage defining region style
     */
    CARTA::RegionStyle ImportStyleParameters(CARTA::RegionType region_type, const std::unordered_map<std::string, std::string>& properties);

    /**
     * @brief Set CARTA AnnotationStyle font parameters from properties.
     * @param[in] properties Map of values in file line after region definition
     * @param[out] annotation_style CARTA AnnotationStyle submessage
     */
    void ImportFontStyleParameters(
        const std::unordered_map<std::string, std::string>& properties, CARTA::AnnotationStyle* annotation_style);

    /**
     * @brief Set CARTA AnnotationStyle parameters for point region.
     * @param[in] symbol_char character describing a casa Symbol shape
     * @param[in] properties Map of values in file line after region definition
     * @param[out] annotation_style CARTA AnnotationStyle submessage
     */
    void ImportPointStyleParameters(const std::string& symbol_char, const std::unordered_map<std::string, std::string>& properties,
        CARTA::AnnotationStyle* annotation_style);

    /**
     * @brief Parse box definition to get RegionState parameters.
     * @param[in] parameters Vector of strings parsed from file line describing region
     * @param[in] coord_frame Direction frame used to define parameter points
     * @param[out] control_points Vector of CARTA::Points in pixel coordinates converted from parameters
     * @param[out] rotation Box rotation in degrees from parameters
     */
    bool GetBoxControlPoints(
        std::vector<std::string>& parameters, std::string& coord_frame, std::vector<CARTA::Point>& control_points, float& rotation);

    /**
     * @brief Convert casacore Quantities for casa centerbox, rotbox, or textbox to CARTA Rectangle control points.
     * @param[in] region Name of region
     * @param[in] cx Value of center x as a casacore Quantity
     * @param[in] cy Value of center y as a casacore Quantity
     * @param[in] width Value of rectangle width as a casacore Quantity
     * @param[in] height Value of rectangle height as a casacore Quantity
     * @param[in] coord_frame Direction frame used to define casacore Quantities
     * @param[out] control_points Vector of CARTA::Points in pixel coordinates defining CARTA Rectangle (center, width, height).
     */
    bool GetCenterBoxPoints(const std::string& region, casacore::Quantity& cx, casacore::Quantity& cy, casacore::Quantity& width,
        casacore::Quantity& height, std::string& coord_frame, std::vector<CARTA::Point>& control_points);

    /**
     * @brief Convert casacore Quantities for casa box corners to CARTA Rectangle control points.
     * @param[in] blcx Box bottom left corner x value as a casacore Quantity
     * @param[in] blcy Box bottom left corner y value as a casacore Quantity
     * @param[in] trcx Box top right corner x value as a casacore Quantity
     * @param[in] trcy Box top right corner y value as a casacore Quantity
     * @param[in] coord_frame Direction frame used to define casacore Quantities
     * @param[out] control_points Vector of CARTA::Points in pixel coordinates defining CARTA Rectangle (center, width, height).
     */
    bool GetRectBoxPoints(casacore::Quantity& blcx, casacore::Quantity& blcy, casacore::Quantity& trcx, casacore::Quantity& trcy,
        std::string& coord_frame, std::vector<CARTA::Point>& control_points);
};

} // namespace carta

#endif // CARTA_SRC_REGION_REGIONIMPORTEXPORT_CRTFIMPORTER_H_
