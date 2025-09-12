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
     * @brief Parse each line of file to import regions
     * @param file_lines Lines read from region file
     */
    void SetFileLineRegions(std::vector<std::string>& file_lines);

    /**
     * @brief Get direction frame coord value in region properties
     * @param properties Map of style parameters
     * @return Region direction frame
     */
    std::string GetRegionDirectionFrame(const std::unordered_map<std::string, std::string>& properties);

    /**
     * @brief Import point or text region.
     * @param parameters Region definition parameters
     * @param coord_frame Direction frame used to define parameters
     * @return region state struct
     */
    RegionState ImportAnnSymbolText(std::vector<std::string>& parameters, std::string& coord_frame);

    /**
     * @brief Import rectangle region.
     * @param parameters Region definition parameters
     * @param coord_frame Direction frame used to define parameters
     * @return region state struct
     */
    RegionState ImportAnnBox(std::vector<std::string>& parameters, std::string& coord_frame);

    /**
     * @brief Import ellipse region.
     * @param parameters Region definition parameters
     * @param coord_frame Direction frame used to define parameters
     * @return region state struct
     */
    RegionState ImportAnnEllipse(std::vector<std::string>& parameters, std::string& coord_frame);

    /**
     * @brief Import polygon or line-based region.
     * @param parameters Region definition parameters
     * @param coord_frame Direction frame used to define parameters
     * @return region state struct
     */
    RegionState ImportAnnPoly(std::vector<std::string>& parameters, std::string& coord_frame);

    /**
     * @brief Import region style parameters for region type.
     * @param region_type Region type
     * @param properties Map of style parameters
     * @return Region style parameters
     */
    CARTA::RegionStyle ImportStyle(CARTA::RegionType region_type, std::unordered_map<std::string, std::string>& properties);

    /**
     * @brief Set annotation style font parameters.
     * @param[in] properties Map of style parameters
     * @param[out] Annotation style parameters
     */
    void ImportFontStyle(std::unordered_map<std::string, std::string>& properties, CARTA::AnnotationStyle* annotation_style);

    /**
     * @brief Set annotation style point parameters.
     * @param[in] symbol_char character describing a casa Symbol shape
     * @param[in] properties Map of style parameters
     * @param[out] Annotation style parameters
     */
    void ImportPointStyle(
        const std::string& symbol_char, std::unordered_map<std::string, std::string>& properties, CARTA::AnnotationStyle* annotation_style);

    /**
     * @brief Convert box definition with all corner points to control points.
     * @param[in] parameters Region definition parameters
     * @param[in] coord_frame Direction frame used to define parameter points
     * @param[out] control_points Points in pixel coordinates
     * @param[out] rotation Box rotation in degrees
     */
    bool GetBoxControlPoints(
        std::vector<std::string>& parameters, std::string& coord_frame, std::vector<CARTA::Point>& control_points, float& rotation);

    /**
     * @brief Convert box definition with center and width/height to control points.
     * @param[in] region Name of region
     * @param[in] cx Center x
     * @param[in] cy Center y
     * @param[in] width Rectangle width
     * @param[in] height Rectangle height
     * @param[in] coord_frame Direction frame used to define casacore Quantities
     * @param[out] control_points Rectangle control points
     */
    bool GetCenterBoxPoints(const std::string& region, casacore::Quantity& cx, casacore::Quantity& cy, casacore::Quantity& width,
        casacore::Quantity& height, std::string& coord_frame, std::vector<CARTA::Point>& control_points);

    /**
     * @brief Convert box definition with blc, trc corners to control points.
     * @param[in] blcx Box bottom left corner x
     * @param[in] blcy Box bottom left corner y
     * @param[in] trcx Box top right corner x
     * @param[in] trcy Box top right corner y
     * @param[in] coord_frame Direction frame used to define corners
     * @param[out] control_points Rectangle control points
     */
    bool GetRectBoxPoints(casacore::Quantity& blcx, casacore::Quantity& blcy, casacore::Quantity& trcx, casacore::Quantity& trcy,
        std::string& coord_frame, std::vector<CARTA::Point>& control_points);
};

} // namespace carta

#endif // CARTA_SRC_REGION_REGIONIMPORTEXPORT_CRTFIMPORTER_H_
