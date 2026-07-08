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
     * @brief Constructor for RegionExporter class
     * @param coord_sys casacore::CoordinateSystem of image from which region is exported
     * @param shape casacore::IPosition shape of image from which region is exported
     */
    RegionExporter(std::shared_ptr<casacore::CoordinateSystem> coord_sys, const casacore::IPosition& shape);

    /** @brief Default destructor */
    virtual ~RegionExporter() = default;

    /**
     * @brief Determine if export file can be created or overwritten.
     * @param[in] filename Name of region file
     * @param[in] overwrite Whether existing file can be overwritten
     * @param[out] export_ack CARTA ExportRegionAck message to complete if error.
     * @return Whether export file can be written.
     */
    bool CanExportToFile(const std::string& filename, bool overwrite, CARTA::ExportRegionAck& export_ack);

    /**
     * @brief Add file line for region
     * @param file_id File id for image from which region is exported
     * @param region Region to export
     * @param region_style Region style parameters
     * @param export_pixels Whether to export region in pixel or world coordinates
     * @return Whether adding the region file line is successful
     */
    bool AddRegion(int file_id, std::shared_ptr<Region> region, const CARTA::RegionStyle& region_style, bool export_pixels);

    /**
     * @brief Export region file lines to file or contents.
     * @param[in] filename Filename to use for export; if empty, export to contents in ack message
     * @param[in] message Any errors from adding regions to add to ack message.
     * @param[out] export_ack Response message to complete
     */
    void ExportRegions(const std::string& filename, std::string& message, CARTA::ExportRegionAck& export_ack);

protected:
    /**
     * @brief Add file line for region in pixel coordinates
     * @param region_state Region definition parameters
     * @param region_style Region style parameters
     * @return Whether adding the region file line is successful
     */
    virtual bool AddRegion(const RegionState& region_state, const CARTA::RegionStyle& region_style) = 0;

    /**
     * @brief Add file line for region in world coordinates or in matched image
     * @param region_type Region type
     * @param control_points Region control points in world coordinates
     * @param rotation Region rotation
     * @param region_style Region style parameters
     * @return Whether adding the region file line is successful
     */
    virtual bool AddRegion(CARTA::RegionType region_type, const std::vector<casacore::Quantity>& control_points,
        const casacore::Quantity& rotation, const CARTA::RegionStyle& region_style) = 0;

    /**
     * @brief Write region file lines to filename.
     * @param[in] filename Name of region file
     * @param[out] error Message describing error if export fails
     * @return Whether writing any region lines is successful
     */
    virtual bool ExportRegions(const std::string& filename, std::string& error) = 0;

    /**
     * @brief Serialise region file lines to vector.
     * @param[out] contents Vector for lines
     * @param[out] error Message describing error if export fails
     * @return Whether writing any region lines is successful
     */
    virtual bool ExportRegions(std::vector<std::string>& contents, std::string& error) = 0;

    /**
     * @brief Add file line for region in world coordinates or for matched image.
     * @param region_state Region definition parameters
     * @param region_style Region style parameters
     * @param region_record casacore Record created from LCRegion applied to image
     * @param export_pixels Whether to export region in pixel or world coordinates
     * @return Whether region is exported successfully
     */
    bool AddRegion(const RegionState& region_state, const CARTA::RegionStyle& region_style, const casacore::RecordInterface& region_record,
        bool export_pixels);

    /**
     * @brief Append compass style parameters to region file line.
     * @param[in] region_style Region style parameters
     * @param[in] coord_frame Coordinate frame for compass region
     * @param[in, out] file_line Line to be appended
     */
    void AddCompassStyle(const CARTA::RegionStyle& region_style, const std::string& coord_frame, std::string& file_line);

    /** @brief Image coordinate system. */
    std::shared_ptr<casacore::CoordinateSystem> _coord_sys;

    /** @brief Image shape. */
    casacore::IPosition _image_shape;

    /** @brief Region names for conversion from CARTA type to CRTF or DS9 name. */
    std::unordered_map<CARTA::RegionType, std::string> _region_names;

    /** @brief casacore image reference frame from coordinate system. */
    std::string _image_coord_frame;

    /** @brief Coordinate frame for file lines. */
    std::string _file_coord_frame;

    /** @brief Formatted lines for header, globals, and regions for region file. */
    std::vector<std::string> _file_lines;

private:
    /**
     * @brief Convert casacore Record to point region control points.
     * @param[in] region_record casacore Record created from casacore LCRegion
     * @param[in] export_pixels Whether to set control points in pixel or world coordinates
     * @param [out] control_points Region control points
     * @return Whether the conversion is successful
     */
    bool ConvertRecordToPoint(
        const casacore::RecordInterface& region_record, bool export_pixels, std::vector<casacore::Quantity>& control_points);

    /**
     * @brief Convert casacore Record to rectangle region control points.
     * @param[in] region_record casacore Record created from casacore LCRegion
     * @param[in] export_pixels Whether to set control points in pixel or world coordinates
     * @param [out] control_points Region control points
     * @return Whether the conversion is successful
     */
    bool ConvertRecordToRectangle(
        const casacore::RecordInterface& region_record, bool export_pixels, std::vector<casacore::Quantity>& control_points);

    /**
     * @brief Convert casacore Record to ellipse/circle region control points and rotation.
     * @param[in] region_state Region definition parameters
     * @param[in] region_record casacore Record created from casacore LCRegion
     * @param[in] export_pixels Whether to set control points in pixel or world coordinates
     * @param[out] control_points Region control points
     * @param[out] qrotation Region rotation
     * @return Whether the conversion is successful
     */
    bool ConvertRecordToEllipse(const RegionState& region_state, const casacore::RecordInterface& region_record, bool export_pixels,
        std::vector<casacore::Quantity>& control_points, casacore::Quantity& qrotation);

    /**
     * @brief Convert casacore Record to annulus region control points and rotation.
     * @param[in] region_state Region definition parameters
     * @param[in] region_record casacore Record created from casacore LCRegion
     * @param[in] export_pixels Whether to set control points in pixel or world coordinates
     * @param[out] control_points Region control points
     * @param[out] qrotation Region rotation
     * @return Whether the conversion is successful
     */
    bool ConvertRecordToAnnulus(const RegionState& region_state, const casacore::RecordInterface& region_record, bool export_pixels,
        std::vector<casacore::Quantity>& control_points, casacore::Quantity& qrotation);

    /**
     * @brief Convert casacore Record to polygon/polyline region control points.
     * @param[in] region_record casacore Record created from casacore LCRegion
     * @param[in] export_pixels Whether to set control points in pixel or world coordinates
     * @param [out] control_points Region control points
     * @return Whether the conversion is successful
     */
    bool ConvertRecordToPolygonLine(
        const casacore::RecordInterface& region_record, bool export_pixels, std::vector<casacore::Quantity>& control_points);

    /**
     * @brief Convert casacore Vector from float to double.
     * @param float_vector Float vector to convert
     * @return vector
     */
    casacore::Vector<casacore::Double> FloatVectorToDouble(const casacore::Vector<casacore::Float>& float_vector);
};

} // namespace carta

#endif // CARTA_SRC_REGION_REGIONIMPORTEXPORT_REGIONEXPORTER_H_
