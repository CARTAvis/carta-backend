/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# RegionExport.h: handle region export in CRTF and DS9 formats

#ifndef CARTA_SRC_REGION_REGIONIMPORTEXPORT_REGIONEXPORT_H_
#define CARTA_SRC_REGION_REGIONIMPORTEXPORT_REGIONEXPORT_H_

#include <imageanalysis/Annotations/AnnotationBase.h>

#include <carta-protobuf/export_region.pb.h>

#include "Region/Region.h"
#include "RegionImportExportUtil.h"

namespace carta {

class RegionExport {
public:
    RegionExport(std::shared_ptr<casacore::CoordinateSystem> image_coord_sys, const casacore::IPosition& image_shape);

    virtual ~RegionExport() = default;

    // Determine if export file can be created or overwritten
    bool CanExportToFile(const std::string& filename, bool overwrite, CARTA::ExportRegionAck& export_ack);

    // Add region to export
    bool AddExportRegion(int file_id, std::shared_ptr<Region> region, const CARTA::RegionStyle& region_style, bool export_pixel_coords);

    // Perform export of added regions to filename or contents
    void ExportRegions(const std::string& filename, std::string& message, CARTA::ExportRegionAck& export_ack);

protected:
    bool AddExportRegion(const RegionState& region_state, const CARTA::RegionStyle& region_style,
        const casacore::RecordInterface& region_record, bool pixel_coord);
    void ExportAnnCompassStyle(const CARTA::RegionStyle& region_style, const std::string& ann_coord_sys, std::string& region_line);

    virtual bool AddExportRegion(const RegionState& region_state, const CARTA::RegionStyle& region_style) = 0;
    virtual bool AddExportRegion(CARTA::RegionType region_type, const std::vector<casacore::Quantity>& control_points,
        const casacore::Quantity& rotation, const CARTA::RegionStyle& region_style) = 0;

    // Print regions to file or string vector
    virtual bool ExportRegions(const std::string& filename, std::string& error) = 0;
    virtual bool ExportRegions(std::vector<std::string>& contents, std::string& error) = 0;

    // Image info to which region is applied
    std::shared_ptr<casacore::CoordinateSystem> _coord_sys;
    casacore::IPosition _image_shape;
    std::unordered_map<CARTA::RegionType, std::string> _region_names;

    std::vector<std::string> _export_regions;

private:
    // Return control_points and rotation Quantity for region type
    bool ConvertRecordToPoint(
        const casacore::RecordInterface& region_record, bool pixel_coord, std::vector<casacore::Quantity>& control_points);
    bool ConvertRecordToRectangle(
        const casacore::RecordInterface& region_record, bool pixel_coord, std::vector<casacore::Quantity>& control_points);
    bool ConvertRecordToEllipse(const RegionState& region_state, const casacore::RecordInterface& region_record, bool pixel_coord,
        std::vector<casacore::Quantity>& control_points, casacore::Quantity& qrotation);
    bool ConvertRecordToPolygonLine(
        const casacore::RecordInterface& region_record, bool pixel_coord, std::vector<casacore::Quantity>& control_points);
};

} // namespace carta

#endif // CARTA_SRC_REGION_REGIONIMPORTEXPORT_REGIONEXPORT_H_
