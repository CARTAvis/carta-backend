/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# CrtfExporter.h: handle CRTF region file export

#ifndef CARTA_SRC_REGION_REGIONIMPORTEXPORT_CRTFEXPORTER_H_
#define CARTA_SRC_REGION_REGIONIMPORTEXPORT_CRTFEXPORTER_H_

#include <imageanalysis/Annotations/AnnRegion.h>
#include <imageanalysis/Annotations/AnnSymbol.h>

#include "RegionExporter.h"

#define REGION_DASH_LENGTH 2

namespace carta {

/** @brief A class for exporting regions in CRTF format. */
class CrtfExporter : public RegionExporter {
public:
    /**
     * @brief Constructor for CrtfExporter class for exporting regions to CRTF file.
     * @param image_coord_sys casacore::CoordinateSystem of image from which region is exported
     * @param image_shape casacore::IPosition describing shape of image from which region is exported
     * @param stokes_axis Current stokes axis index in image from which region is exported, for exporting stokes type
     */
    CrtfExporter(std::shared_ptr<casacore::CoordinateSystem> image_coord_sys, const casacore::IPosition& image_shape, int stokes_axis);

protected:
    /**
     * @brief Export region in pixel coordinates using RegionState and RegionStyle to _export_regions.
     * @param region_state RegionState struct defining region parameters
     * @param region_style CARTA RegionStyle submessage defining region style
     * @return Whether the region export is successful
     */
    bool AddExportRegion(const RegionState& region_state, const CARTA::RegionStyle& region_style) override;

    /**
     * @brief Export region in world coordinates using casacore Quantities and RegionStyle to _export_regions.
     * @param region_type CARTA RegionType enum defining type of region
     * @param control_points Region control points as casacore Quantities in world coordinates
     * @param rotation Region rotation as casacore Quantity
     * @param region_style CARTA RegionStyle submessage defining region style
     * @return Whether the region export is successful
     */
    bool AddExportRegion(CARTA::RegionType region_type, const std::vector<casacore::Quantity>& control_points,
        const casacore::Quantity& rotation, const CARTA::RegionStyle& region_style) override;

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
     * @brief Create casa AnnotationBase or AnnRegion from region defined in world coordinates.
     * @param[in] region_type CARTA RegionType enum defining type of region
     * @param[in] control_points Region control points as casacore Quantities in world coordinates
     * @param[in] rotation Region rotation as casacore Quantity
     * @param[in] region_style CARTA RegionStyle submessage defining region style
     * @param[out] ann_base Pointer to return new AnnotationBase region
     * @param[out] ann_region Pointer to return new AnnRegion region
     * @return Whether the region creation is successful
     */
    bool GetAnnRegion(CARTA::RegionType region_type, const std::vector<casacore::Quantity>& control_points,
        const casacore::Quantity& rotation, const CARTA::RegionStyle& region_style, casa::AnnotationBase*& ann_base,
        casa::AnnRegion*& ann_region);

    /**
     * @brief Add region line to _export_regions after adjusting casa region name if needed and adding style.
     * @param region_type CARTA RegionType enum defining type of region
     * @param region_line CRTF-formatted string for file line, printed from casa annotation region
     * @param region_style CARTA RegionStyle submessage defining region style
     * @return Whether adding the region is successful
     */
    bool AddRegionExportLine(CARTA::RegionType region_type, std::string& region_line, const CARTA::RegionStyle& region_style);

    /**
     * @brief Add region lines to _export_regions for text region, as text box file line then text file line
     * @param region_type CARTA RegionType enum defining type of region
     * @param region_line CRTF-formatted string for file line, printed from casa annotation box region
     * @param control_points Region control points as casacore Quantities in world coordinates for text
     * @param region_style CARTA RegionStyle submessage defining region style
     * @return Whether adding the region is successful
     */
    bool AddTextRegionExportLines(CARTA::RegionType region_type, std::string& region_line,
        const std::vector<casacore::Quantity>& control_points, const CARTA::RegionStyle& region_style);

    /**
     * @brief Fix region_line in place, replacing misspelled fontstyle output by casa to fontstyle imported by casa.
     * @param[in, out] region_line CRTF-formatted string for file line, printed from casa annotation region
     */
    void FixFontstyle(std::string& region_line);

    /**
     * @brief Convert CARTA point shape enum to casa AnnSymbol enum.
     * @param point_shape CARTA PointAnnotationShape enum value
     * @return casa::AnnSymbol::Symbol enum value corresponding to input point shape
     */
    casa::AnnSymbol::Symbol GetAnnSymbol(CARTA::PointAnnotationShape point_shape);

    /**
     * @brief Convert CARTA point shape enum to character imported by casa.
     * @param point_shape CARTA PointAnnotationShape enum value
     * @return char corresponding to input point shape
     */
    char GetAnnSymbolCharacter(CARTA::PointAnnotationShape point_shape);

    /**
     * @brief Convert RegionStyle color to formatted string
     * @param region_style CARTA RegionStyle submessage defining region style
     * @return Converted color string
     */
    std::string GetRegionColor(const CARTA::RegionStyle& region_style);

    /**
     * @brief Convert region style dash list to casa annotation line style else set default.
     * @param region_style CARTA RegionStyle submessage defining region style
     * @return casa::AnnotationBase::LineStyle enum representing dash style
     */
    casa::AnnotationBase::LineStyle GetRegionLineStyle(const CARTA::RegionStyle& region_style);

    /**
     * @brief Extract font parameters from region style else set defaults.
     * @param[in] region_style CARTA RegionStyle submessage defining region style
     * @param[out] font font name
     * @param[out] font_size font size
     * @param[out] font_style casa::AnnotationBase::FontStyle enum value
     */
    void GetAnnotationFontParameters(
        const CARTA::RegionStyle& region_style, std::string& font, unsigned int& font_size, casa::AnnotationBase::FontStyle& font_style);

    /**
     * @brief Extract point parameters from region style else set defaults.
     * @param[in] region_style CARTA RegionStyle submessage defining region style
     * @param[out] symbol_size return point size
     * @param[out] symbol_thickness return thickness of point outline.
     */
    void GetAnnotationSymbolParameters(const CARTA::RegionStyle& region_style, unsigned int& symbol_size, unsigned int& symbol_thickness);

    /**
     * @brief Set export coordinate frame value from coordinate system direction coordinate or linear coordinate.
     * @return Coordinate frame.
     */
    std::string GetAnnotationCoordinateSystem();

    /**
     * @brief Add standard CRTF keywords or region type-specific parameters and optional label to region line.
     * @param[in] region_style CARTA RegionStyle submessage defining region style
     * @param[in, out] region_line CRTF-formatted string for file line, printed from casa annotation region
     */
    void ExportStyleParameters(const CARTA::RegionStyle& region_style, std::string& region_line);

    /**
     * @brief Set region style parameters in AnnotationBase region, for printing by casa to region line.
     * @param[in] region_style CARTA RegionStyle submessage defining region style
     * @param[in, out] region Pointer to casa AnnotationBase region in which style parameters are set
     */
    void ExportStyleParameters(const CARTA::RegionStyle& region_style, casa::AnnotationBase* region);

    /**
     * @brief Get casacore stokes types for stokes axis set in constructor.
     * @return casacore StokesTypes enum vector
     */
    casacore::Vector<casacore::Stokes::StokesTypes> GetStokesTypes();

    /**
     * @brief Get CRTF file header string containing casa RegionTextParser version.
     * @return CRTF file header string
     */
    std::string GetCrtfVersionHeader();

    /** @brief stokes axis index for current stokes, set from constructor parameter for adding stokes type to export. */
    int _stokes_axis;
};

} // namespace carta

#endif // CARTA_SRC_REGION_REGIONIMPORTEXPORT_CRTFEXPORTER_H_
