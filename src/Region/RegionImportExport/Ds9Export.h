/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# Ds9Export.h: handle DS9 region file export

#ifndef CARTA_SRC_REGION_REGIONIMPORTEXPORT_DS9EXPORT_H_
#define CARTA_SRC_REGION_REGIONIMPORTEXPORT_DS9EXPORT_H_

#include "RegionExport.h"

namespace carta {

class Ds9Export : public RegionExport {
public:
    Ds9Export(std::shared_ptr<casacore::CoordinateSystem> image_coord_sys, const casacore::IPosition& image_shape, bool pixel_coords);

protected:
    // Export using RegionState pixel control points
    bool AddExportRegion(const RegionState& region_state, const CARTA::RegionStyle& region_style) override;

    // Export using Quantity world control points
    bool AddExportRegion(CARTA::RegionType region_type, const std::vector<casacore::Quantity>& control_points,
        const casacore::Quantity& rotation, const CARTA::RegionStyle& style) override;

    // Print regions to file or contents
    bool ExportRegions(const std::string& filename, std::string& error) override;
    bool ExportRegions(std::vector<std::string>& contents, std::string& error) override;

private:
    void InitGlobalProperties();
    void InitDs9CoordMap();
    void SetImageReferenceFrame();

    void AddHeader();
    std::string AddExportRegionPixel(CARTA::RegionType region_type, const std::vector<casacore::Quantity>& control_points, float angle,
        const CARTA::RegionStyle& region_style);
    std::string AddExportRegionWorld(CARTA::RegionType region_type, const std::vector<casacore::Quantity>& control_points, float angle,
        const CARTA::RegionStyle& region_style);
    void ExportStyleParameters(const CARTA::RegionStyle& region_style, std::string& region_line);
    void ExportTextboxStyleParameters(const CARTA::RegionStyle& region_style, std::string& region_line);
    void ExportFontParameters(const CARTA::RegionStyle& region_style, std::string& region_line);
    void ExportAnnotationStyleParameters(CARTA::RegionType region_type, const CARTA::RegionStyle& region_style, std::string& region_line);
    void ExportAnnPointParameters(const CARTA::RegionStyle& region_style, std::string& region_line);

    bool _pixel_coords;
    // DS9/casacore conversion
    std::unordered_map<std::string, std::string> _coord_map; // ds9 to casacore
    std::string _image_ref_frame;                            // casacore
    std::string _file_ref_frame;                             // DS9

    // Default properties
    std::unordered_map<std::string, std::string> _global_properties;
};

} // namespace carta

#endif // CARTA_SRC_REGION_REGIONIMPORTEXPORT_DS9EXPORT_H_
