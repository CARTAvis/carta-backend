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
     * @param coord_sys casacore::CoordinateSystem of image from which region is exported
     * @param shape casacore::IPosition describing shape of image from which region is exported
     * @param stokes_axis Current stokes axis in image from which region is exported
     */
    CrtfExporter(std::shared_ptr<casacore::CoordinateSystem> coord_sys, const casacore::IPosition& shape, int stokes_axis);

protected:
    /**
     * @brief Add file line for region in pixel coordinates
     * @param region_state Region definition parameters
     * @param region_style Region style parameters
     * @return Whether adding the region file line is successful
     */
    bool AddRegion(const RegionState& region_state, const CARTA::RegionStyle& region_style) override;

    /**
     * @brief Add file line for region in world coordinates or in matched image
     * @param region_type Region type
     * @param control_points Region control points in world coordinates
     * @param rotation Region rotation
     * @param region_style Region style parameters
     * @return Whether adding the region file line is successful
     */
    bool AddRegion(CARTA::RegionType region_type, const std::vector<casacore::Quantity>& control_points, const casacore::Quantity& rotation,
        const CARTA::RegionStyle& region_style) override;

    /**
     * @brief Write region file lines to filename.
     * @param[in] filename Name of region file
     * @param[out] error Message describing error if export fails
     * @return Whether writing any region lines is successful
     */
    bool ExportRegions(const std::string& filename, std::string& error) override;

    /**
     * @brief Serialise region file lines to vector.
     * @param[out] contents Vector for lines
     * @param[out] error Message describing error if export fails
     * @return Whether writing any region lines is successful
     */
    bool ExportRegions(std::vector<std::string>& contents, std::string& error) override;

private:
    /**
     * @brief Create casa AnnotationBase or AnnRegion from region parameters.
     * @param[in] region_type Region type
     * @param[in] control_points Region control points
     * @param[in] rotation Region rotation
     * @param[in] region_style Region style parameters
     * @param[out] ann_base Pointer to return new AnnotationBase region
     * @param[out] ann_region Pointer to return new AnnRegion region
     * @return Whether the region was created
     */
    bool GetAnnRegion(CARTA::RegionType region_type, const std::vector<casacore::Quantity>& control_points,
        const casacore::Quantity& rotation, const CARTA::RegionStyle& region_style, casa::AnnotationBase*& ann_base,
        casa::AnnRegion*& ann_region);

    /**
     * @brief Add file line after adjusting line printed by CASA region
     * @param region_type Region type
     * @param file_line Line to be adjusted and added
     * @param region_style Region style parameters
     * @return Whether adding the file line is successful
     */
    bool AddFileLine(CARTA::RegionType region_type, std::string& file_line, const CARTA::RegionStyle& region_style);

    /**
     * @brief Add file lines for text region, as textbox file line then text file line
     * @param region_type Region type
     * @param file_line Line for textbox region
     * @param control_points Region control points for text region
     * @param region_style Region style parameters
     * @return Whether adding the file lines is successful
     */
    bool AddTextFileLines(CARTA::RegionType region_type, std::string& file_line, const std::vector<casacore::Quantity>& control_points,
        const CARTA::RegionStyle& region_style);

    /**
     * @brief Fix fontstyle added by CASA region.
     * @param[in, out] file_line Line to be fixed
     */
    void FixFontstyle(std::string& file_line);

    /**
     * @brief Convert CARTA point shape to CASA annotation symbol.
     * @param point_shape CARTA point shape
     * @return CASA annotation symbol
     */
    casa::AnnSymbol::Symbol GetAnnSymbol(CARTA::PointAnnotationShape point_shape);

    /**
     * @brief Convert CARTA point shape to CASA symbol character.
     * @param point_shape CARTA point shape
     * @return char representing CASA symbol shape
     */
    char GetAnnSymbolCharacter(CARTA::PointAnnotationShape point_shape);

    /**
     * @brief Format color for file line
     * @param color Region color
     * @return Formatted color string
     */
    std::string FormatColor(const std::string& color);

    /**
     * @brief Convert dash list to CASA line style else set default.
     * @param region_style Region style parameters
     * @return CASA line style
     */
    casa::AnnotationBase::LineStyle GetLineStyle(const CARTA::RegionStyle& region_style);

    /**
     * @brief Extract font parameters from region style else set defaults.
     * @param[in] region_style Region style parameters
     * @param[out] font Font name
     * @param[out] font_size Font size
     * @param[out] font_style Font style enum
     */
    void GetFontStyle(
        const CARTA::RegionStyle& region_style, std::string& font, unsigned int& font_size, casa::AnnotationBase::FontStyle& font_style);

    /**
     * @brief Extract CASA symbol style parameters from region style else set defaults.
     * @param[in] region_style Region style parameters
     * @param[out] symbol_size Symbol size
     * @param[out] symbol_thickness Symbol outline thickness.
     */
    void GetSymbolStyle(const CARTA::RegionStyle& region_style, unsigned int& symbol_size, unsigned int& symbol_thickness);

    /**
     * @brief Add CRTF style parameters common to all regions to file line.
     * @param[in] region_style Region style
     * @param[in, out] file_line File line to append
     */
    void AddStyle(const CARTA::RegionStyle& region_style, std::string& file_line);

    /**
     * @brief Set region style parameters in AnnotationBase region
     * @param[in] region_style Region style parameters
     * @param[in, out] region Pointer to casa AnnotationBase region (AnnotationBase or AnnRegion)
     */
    void SetAnnotationRegionStyle(const CARTA::RegionStyle& region_style, casa::AnnotationBase* region);

    /**
     * @brief Get casacore stokes types for stokes axis set in constructor.
     * @return casacore StokesTypes enum vector
     */
    casacore::Vector<casacore::Stokes::StokesTypes> GetStokesTypes();

    /**
     * @brief Get CRTF file header string.
     * @return header
     */
    std::string GetFileHeader();

    /** @brief stokes axis index for current stokes, set from constructor parameter for adding stokes type to export. */
    int _stokes_axis;
};

} // namespace carta

#endif // CARTA_SRC_REGION_REGIONIMPORTEXPORT_CRTFEXPORTER_H_
