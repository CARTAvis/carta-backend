/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# Ds9Exporter.h: handle DS9 region file export

#ifndef CARTA_SRC_REGION_REGIONIMPORTEXPORT_DS9EXPORTER_H_
#define CARTA_SRC_REGION_REGIONIMPORTEXPORT_DS9EXPORTER_H_

#include "RegionExporter.h"

namespace carta {

/** @brief A class for exporting regions in DS9 format. */
class Ds9Exporter : public RegionExporter {
public:
    /**
     * @brief Constructor for Ds9Exporter class for exporting regions to DS9 file.
     * @param coord_sys casacore::CoordinateSystem of image from which region is exported
     * @param shape casacore::IPosition describing shape of image from which region is exported
     * @param export_pixels Whether to export in pixel or world coordinates.
     */
    Ds9Exporter(std::shared_ptr<casacore::CoordinateSystem> coord_sys, const casacore::IPosition& shape, bool export_pixels);

protected:
    /**
     * @brief Add file line for region in pixel coordinates.
     * @param region_state Region definition parameters
     * @param region_style Region style parameters
     * @return Whether adding the file line is successful
     */
    bool AddRegion(const RegionState& region_state, const CARTA::RegionStyle& region_style) override;

    /**
     * @brief Add file line for region in world coordinates or in matched image.
     * @param region_type Region type
     * @param control_points Region control points in world coordinates
     * @param rotation Region rotation
     * @param region_style Region style parameters
     * @return Whether adding the file line is successful
     */
    bool AddRegion(CARTA::RegionType region_type, const std::vector<casacore::Quantity>& control_points, const casacore::Quantity& rotation,
        const CARTA::RegionStyle& region_style) override;

    /**
     * @brief Write region file lines to filename.
     * @param[in] filename Name of region file
     * @param[out] error Message describing error if export fails
     * @return Whether writing any file lines is successful
     */
    bool ExportRegions(const std::string& filename, std::string& error) override;

    /**
     * @brief Set contents to region file lines.
     * @param[out] contents Vector for lines
     * @param[out] error Message describing error if export fails
     * @return Whether writing any file lines is successful
     */
    bool ExportRegions(std::vector<std::string>& contents, std::string& error) override;

private:
    /**
     * @brief Set global properties to DS9 defaults.
     */
    void InitGlobalProperties();

    /**
     * @brief Set frame to use for file lines from image coordinate system.
     */
    void SetFileCoordFrame();

    /**
     * @brief Return DS9 file header lines.
     */
    std::vector<std::string> GetFileHeader();

    /**
     * @brief Create region file line using pixel coordinates.
     * @param region_type Region type
     * @param control_points Region control points in pixel coordinates
     * @param rotation Region rotation in degrees
     * @param region_style Region style parameters
     * @return file line for region
     */
    std::string GetRegionPixelLine(CARTA::RegionType region_type, const std::vector<casacore::Quantity>& control_points, float rotation,
        const CARTA::RegionStyle& region_style);

    /**
     * @brief Create region file line using world coordinates.
     * @param region_type Region type
     * @param control_points Region control points in world coordinates
     * @param rotation Region rotation in degrees
     * @param region_style Region style parameters
     * @return file line for region
     */
    std::string GetRegionWorldLine(CARTA::RegionType region_type, const std::vector<casacore::Quantity>& control_points, float rotation,
        const CARTA::RegionStyle& region_style);

    /**
     * @brief Add DS9 style parameters common to all region types to file line.
     * @param[in] region_style Region style parameters
     * @param[in, out] file_line File line to append
     */
    void AddStyle(const CARTA::RegionStyle& region_style, std::string& file_line);

    /**
     * @brief Add textbox region style to file line.
     * @param[in] region_style Region style parameters
     * @param[in, out] file_line File line to append
     */
    void AddTextboxStyle(const CARTA::RegionStyle& region_style, std::string& file_line);

    /**
     * @brief Add font style parameters to file line.
     * @param[in] region_style Region style parameters
     * @param[in, out] file_line File line to append
     */
    void AddFontStyle(const CARTA::RegionStyle& region_style, std::string& file_line);

    /**
     * @brief Add annotation style parameters to file line.
     * @param[in] region_type Region type
     * @param[in] region_style Region style parameters
     * @param[in, out] file_line File line to append
     */
    void AddAnnotationStyle(CARTA::RegionType region_type, const CARTA::RegionStyle& region_style, std::string& file_line);

    /**
     * @brief Add annotation point region style to file line.
     * @param[in] region_style Region style parameters
     * @param[in, out] file_line File line to append
     */
    void AddAnnPointStyle(const CARTA::RegionStyle& region_style, std::string& file_line);

    /** @brief Whether to export in pixel or world coordinates. */
    bool _export_pixels;

    /** @brief Default DS9 properties. */
    std::unordered_map<std::string, std::string> _global_properties;
};

} // namespace carta

#endif // CARTA_SRC_REGION_REGIONIMPORTEXPORT_DS9EXPORTER_H_
