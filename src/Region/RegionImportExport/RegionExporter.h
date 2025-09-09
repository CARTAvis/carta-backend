/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# RegionExporter.h: handle region export in CRTF and DS9 formats

#ifndef CARTA_SRC_REGION_REGIONIMPORTEXPORT_REGIONEXPORTER_H_
#define CARTA_SRC_REGION_REGIONIMPORTEXPORT_REGIONEXPORTER_H_

#include <imageanalysis/Annotations/AnnotationBase.h>

#include <carta-protobuf/export_region.pb.h>

#include "Region/Region.h"
#include "RegionImportExportUtil.h"

namespace carta {

/** @brief Base class for exporting regions to file. */
class RegionExporter {
public:
    /**
     * @brief Constructor for RegionExporter class to set image coordinate system and shape.
     * @param image_coord_sys casacore::CoordinateSystem of image from which region is exported
     * @param image_shape casacore::IPosition describing shape of image from which region is exported
     */
    RegionExporter(std::shared_ptr<casacore::CoordinateSystem> image_coord_sys, const casacore::IPosition& image_shape);

    /** @brief Default destructor */
    virtual ~RegionExporter() = default;

    /**
     * @brief Determine if export file can be created or overwritten.
     * @param[in] filename Name of file to which regions will be exported
     * @param[in] overwrite Whether existing file can be overwritten
     * @param[out] export_ack CARTA ExportRegionAck message to complete if error.
     * @return Whether export file can be written.
     */
    bool CanExportToFile(const std::string& filename, bool overwrite, CARTA::ExportRegionAck& export_ack);

    /**
     * @brief Export region to file line in _export_regions.
     * @param file_id File id for export image, to determine if reference or matched image
     * @param region Region to be exported
     * @param region_style CARTA RegionStyle submessage defining region style
     * @param export_pixel_coords Whether to export region in pixel or world coordinates
     * @return Whether region is exported successfully
     */
    bool AddExportRegion(int file_id, std::shared_ptr<Region> region, const CARTA::RegionStyle& region_style, bool export_pixel_coords);

    /**
     * @brief Export region file lines in _export_regions to file or contents.
     * @param[in] filename Filename to use for export; if empty, export to contents set in export_ack
     * @param[in] message Error messages from region exports.
     * @param[out] export_ack CARTA ExportRegionAck message to complete
     */
    void ExportRegions(const std::string& filename, std::string& message, CARTA::ExportRegionAck& export_ack);

protected:
    /**
     * @brief Export region to file line in _export_regions from casacore Record, for world coordinates or matched image.
     * @param region_state RegionState struct defining region parameters
     * @param region_style CARTA RegionStyle submessage defining region style
     * @param region_record casacore Record created from LCRegion for region in matched image
     * @param pixel_coord Whether to export region in pixel or world coordinates
     * @return Whether region is exported successfully
     */
    bool AddExportRegion(const RegionState& region_state, const CARTA::RegionStyle& region_style,
        const casacore::RecordInterface& region_record, bool pixel_coord);

    /**
     * @brief Add region style compass parameters to region line.
     * @param[in] region_style CARTA RegionStyle submessage defining region style
     * @param[in] ann_coord_sys Coordinate system for compass annotation region
     * @param[in, out] region_line File line to be appended
     */
    void ExportAnnCompassStyle(const CARTA::RegionStyle& region_style, const std::string& ann_coord_sys, std::string& region_line);

    /**
     * @brief Export region in pixel coordinates using RegionState and RegionStyle to _export_regions.
     * @param region_state RegionState struct defining region parameters
     * @param region_style CARTA RegionStyle submessage defining region style
     * @return Whether the region export is successful
     */
    virtual bool AddExportRegion(const RegionState& region_state, const CARTA::RegionStyle& region_style) = 0;

    /**
     * @brief Export region in world coordinates using casacore Quantities and RegionStyle to _export_regions.
     * @param region_type CARTA RegionType enum defining type of region
     * @param control_points Region control points as casacore Quantities in world coordinates
     * @param rotation Region rotation as casacore Quantity
     * @param region_style CARTA RegionStyle submessage defining region style
     * @return Whether the region export is successful
     */
    virtual bool AddExportRegion(CARTA::RegionType region_type, const std::vector<casacore::Quantity>& control_points,
        const casacore::Quantity& rotation, const CARTA::RegionStyle& region_style) = 0;

    /**
     * @brief Export region file lines in _export_regions to file.
     * @param[in] filename Name of region file for export
     * @param[out] error Message describing error if export to filename fails
     * @return Whether the region export is successful
     */
    virtual bool ExportRegions(const std::string& filename, std::string& error) = 0;

    /**
     * @brief Export region file lines in _export_regions to vector.
     * @param[out] contents Vector to hold region strings
     * @param[out] error Message describing error if export to contents fails
     * @return Whether the region export is successful
     */
    virtual bool ExportRegions(std::vector<std::string>& contents, std::string& error) = 0;

    /** @brief Image coordinate system, from constructor. */
    std::shared_ptr<casacore::CoordinateSystem> _coord_sys;

    /** @brief Image shape, from constructor. */
    casacore::IPosition _image_shape;

    /** @brief Region names for conversion from CARTA type to CRTF or DS9 name. */
    std::unordered_map<CARTA::RegionType, std::string> _region_names;

    /** @brief Regions to be exported as file lines. */
    std::vector<std::string> _export_regions;

private:
    /**
     * @brief Convert casacore Record to point region control points.
     * @param[in] region_record casacore Record created from casacore LCRegion
     * @param[in] pixel_coord Whether to set control points in pixel or world coordinates
     * @param [out] control_points Region control points as casacore Quantities
     * @return Whether the conversion is successful
     */
    bool ConvertRecordToPoint(
        const casacore::RecordInterface& region_record, bool pixel_coord, std::vector<casacore::Quantity>& control_points);

    /**
     * @brief Convert casacore Record to rectangle region control points.
     * @param[in] region_record casacore Record created from casacore LCRegion
     * @param[in] pixel_coord Whether to set control points in pixel or world coordinates
     * @param [out] control_points Region control points as casacore Quantities
     * @return Whether the conversion is successful
     */
    bool ConvertRecordToRectangle(
        const casacore::RecordInterface& region_record, bool pixel_coord, std::vector<casacore::Quantity>& control_points);

    /**
     * @brief Convert casacore Record to ellipse/circle region control points.
     * @param[in] region_state RegionState struct defining region parameters
     * @param[in] region_record casacore Record created from casacore LCRegion
     * @param[in] pixel_coord Whether to set control points in pixel or world coordinates
     * @param[out] control_points Region control points as casacore Quantities
     * @param[out] qrotation Region rotation as casacore Quantity
     * @return Whether the conversion is successful
     */
    bool ConvertRecordToEllipse(const RegionState& region_state, const casacore::RecordInterface& region_record, bool pixel_coord,
        std::vector<casacore::Quantity>& control_points, casacore::Quantity& qrotation);

    /**
     * @brief Convert casacore Record to polygon/polyline region control points.
     * @param[in] region_record casacore Record created from casacore LCRegion
     * @param[in] pixel_coord Whether to set control points in pixel or world coordinates
     * @param [out] control_points Region control points as casacore Quantities
     * @return Whether the conversion is successful
     */
    bool ConvertRecordToPolygonLine(
        const casacore::RecordInterface& region_record, bool pixel_coord, std::vector<casacore::Quantity>& control_points);
};

} // namespace carta

#endif // CARTA_SRC_REGION_REGIONIMPORTEXPORT_REGIONEXPORTER_H_
