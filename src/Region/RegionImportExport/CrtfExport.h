/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# CrtfExport.h: handle CRTF region file export

#ifndef CARTA_SRC_REGION_REGIONIMPORTEXPORT_CRTFEXPORT_H_
#define CARTA_SRC_REGION_REGIONIMPORTEXPORT_CRTFEXPORT_H_

#include <imageanalysis/Annotations/AnnRegion.h>
#include <imageanalysis/Annotations/AnnSymbol.h>

#include "RegionExport.h"

#define REGION_DASH_LENGTH 2

namespace carta {

class CrtfExport : public RegionExport {
public:
    CrtfExport(std::shared_ptr<casacore::CoordinateSystem> image_coord_sys, const casacore::IPosition& image_shape, int stokes_axis);

protected:
    // Export using RegionState pixel control points
    bool AddExportRegion(const RegionState& region_state, const CARTA::RegionStyle& region_style) override;

    // Export using Quantities world control points
    bool AddExportRegion(CARTA::RegionType region_type, const std::vector<casacore::Quantity>& control_points,
        const casacore::Quantity& rotation, const CARTA::RegionStyle& region_style) override;

    // Print regions to file or string vector
    bool ExportRegions(const std::string& filename, std::string& error) override;
    bool ExportRegions(std::vector<std::string>& contents, std::string& error) override;

private:
    // Export region: casa AnnotationBase or AnnRegion
    bool GetAnnRegion(CARTA::RegionType region_type, const std::vector<casacore::Quantity>& control_points,
        const casacore::Quantity& rotation, const CARTA::RegionStyle& region_style, casa::AnnotationBase*& ann_base,
        casa::AnnRegion*& ann_region);
    bool AddRegionExportLine(CARTA::RegionType region_type, std::string& region_line, const CARTA::RegionStyle& region_style);
    bool AddTextRegionExportLines(CARTA::RegionType region_type, std::string& region_line,
        const std::vector<casacore::Quantity>& control_points, const CARTA::RegionStyle& region_style);
    void FixFontstyle(std::string& region_line);

    // Export symbol
    casa::AnnSymbol::Symbol GetAnnSymbol(CARTA::PointAnnotationShape point_shape);
    char GetAnnSymbolCharacter(CARTA::PointAnnotationShape point_shape);

    // Export style parameters
    std::string GetRegionColor(const CARTA::RegionStyle& region_style);
    casa::AnnotationBase::LineStyle GetRegionLineStyle(const CARTA::RegionStyle& region_style);
    void GetAnnotationFontParameters(
        const CARTA::RegionStyle& region_style, std::string& font, unsigned int& font_size, casa::AnnotationBase::FontStyle& font_style);
    void GetAnnotationSymbolParameters(const CARTA::RegionStyle& region_style, unsigned int& symbol_size, unsigned int& symbol_thickness);
    std::string GetAnnotationCoordinateSystem();
    void ExportStyleParameters(const CARTA::RegionStyle& region_style, std::string& region_line);
    void ExportStyleParameters(const CARTA::RegionStyle& region_style, casa::AnnotationBase* region);

    // AnnRegion parameter for export
    casacore::Vector<casacore::Stokes::StokesTypes> GetStokesTypes();

    // Create header when printing region file
    std::string GetCrtfVersionHeader();

    // AnnRegion needs StokesTypes parameter; fallback if coord sys has no StokesCoordinate
    int _stokes_axis;
};

} // namespace carta

#endif // CARTA_SRC_REGION_REGIONIMPORTEXPORT_CRTFEXPORT_H_
