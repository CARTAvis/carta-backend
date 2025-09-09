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
     * @param image_coord_sys casacore::CoordinateSystem of image from which region is exported
     * @param image_shape casacore::IPosition describing shape of image from which region is exported
     * @param pixel_coords Whether to export in pixel or world coordinates.
     */
    Ds9Exporter(std::shared_ptr<casacore::CoordinateSystem> image_coord_sys, const casacore::IPosition& image_shape, bool pixel_coords);

protected:
    /**
     * @brief Export region in pixel coordinates using RegionState and RegionStyle to _export_regions.
     * @param region_state RegionState struct defining region parameters
     * @param region_style CARTA RegionStyle submessage defining region style
     * @return Whether the region export is successful
     */
    bool AddExportRegion(const RegionState& region_state, const CARTA::RegionStyle& region_style) override;

    /**
     * @brief Export in world coordinates using casacore Quantities and region style to _export_regions.
     * @param region_type CARTA RegionType enum defining type of region
     * @param control_points Region control points as casacore Quantities in world coordinates
     * @param rotation Quantity describing region rotation
     * @param region_style CARTA RegionStyle submessage defining region style
     * @return Whether the region export is successful
     */
    bool AddExportRegion(CARTA::RegionType region_type, const std::vector<casacore::Quantity>& control_points,
        const casacore::Quantity& rotation, const CARTA::RegionStyle& style) override;

    /**
     * @brief Export region file lines in _export_regions to file.
     * @param[in] filename Name of region file for export
     * @param[out] error Message describing error if export to filename fails
     * @return Whether the region export is successful
     */
    bool ExportRegions(const std::string& filename, std::string& error) override;

    /**
     * @brief Export region file lines in _export_regions to vector.
     * @param[out] contents Vector to hold region strings
     * @param[out] error Message describing error if export to contents fails
     * @return Whether the region export is successful
     */
    bool ExportRegions(std::vector<std::string>& contents, std::string& error) override;

private:
    /**
     * @brief Set global properties to DS9 defaults
     */
    void InitGlobalProperties();

    /**
     * @brief Set image direction frame from constructor image coordinate system
     */
    void SetImageReferenceFrame();

    /**
     * @brief Set DS9 file header string containing CARTA version and globals.
     */
    void AddHeader();

    /**
     * @brief Create region line using casacore Quantities in pixel coordinates.
     * @param region_type CARTA RegionType enum defining type of region
     * @param control_points Region control points as casacore Quantities in pixel coordinates
     * @param angle Region rotation in degrees
     * @param region_style CARTA RegionStyle submessage defining region style, for textbox export
     * @return region line for _export_regions
     */
    std::string AddExportRegionPixel(CARTA::RegionType region_type, const std::vector<casacore::Quantity>& control_points, float angle,
        const CARTA::RegionStyle& region_style);

    /**
     * @brief Create region line using casacore Quantities in world coordinates.
     * @param region_type CARTA RegionType enum defining type of region
     * @param control_points Region control points as casacore Quantities in world coordinates
     * @param angle Region rotation in degrees
     * @param region_style CARTA RegionStyle submessage defining region style, for textbox export
     * @return region line for _export_regions
     */
    std::string AddExportRegionWorld(CARTA::RegionType region_type, const std::vector<casacore::Quantity>& control_points, float angle,
        const CARTA::RegionStyle& region_style);

    /**
     * @brief Add region style to region line.
     * @param[in] region_style CARTA RegionStyle submessage defining region style
     * @param[in, out] region_line File line to be appended
     */
    void ExportStyleParameters(const CARTA::RegionStyle& region_style, std::string& region_line);

    /**
     * @brief Add textbox region style to region line.
     * @param[in] region_style CARTA RegionStyle submessage defining region style
     * @param[in, out] region_line File line to be appended
     */
    void ExportTextboxStyleParameters(const CARTA::RegionStyle& region_style, std::string& region_line);

    /**
     * @brief Add region style font parameters to region line.
     * @param[in] region_style CARTA RegionStyle submessage defining region style
     * @param[in, out] region_line File line to be appended
     */
    void ExportFontParameters(const CARTA::RegionStyle& region_style, std::string& region_line);

    /**
     * @brief Add region style annotation parameters to region line.
     * @param[in] region_type CARTA RegionType enum defining type of region
     * @param[in] region_style CARTA RegionStyle submessage defining region style
     * @param[in, out] region_line File line to be appended
     */
    void ExportAnnotationStyleParameters(CARTA::RegionType region_type, const CARTA::RegionStyle& region_style, std::string& region_line);

    /**
     * @brief Add region style point parameters to region line.
     * @param[in] region_style CARTA RegionStyle submessage defining region style
     * @param[in, out] region_line File line to be appended
     */
    void ExportAnnPointParameters(const CARTA::RegionStyle& region_style, std::string& region_line);

    /** @brief Whether to export in pixel or world coordinates, from constructor. */
    bool _pixel_coords;
    /** @brief Map from DS9 to casacore coordinate frames. */
    std::unordered_map<std::string, std::string> _coord_map;
    /** @brief casacore image reference frame from coordinate system. */
    std::string _image_ref_frame;
    /** @brief DS9 coordinate frame for export. */
    std::string _file_ref_frame;
    /** @brief Default DS9 properties. */
    std::unordered_map<std::string, std::string> _global_properties;
};

} // namespace carta

#endif // CARTA_SRC_REGION_REGIONIMPORTEXPORT_DS9EXPORTER_H_
