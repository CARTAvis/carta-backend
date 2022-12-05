/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# CrtfImportExport.h: handle CRTF region file import and export

#ifndef CARTA_BACKEND_REGION_CRTFIMPORTEXPORT_H_
#define CARTA_BACKEND_REGION_CRTFIMPORTEXPORT_H_

#include <casacore/casa/Arrays/IPosition.h>
#include <casacore/coordinates/Coordinates/CoordinateSystem.h>
#include <imageanalysis/Annotations/AnnotationBase.h>
#include <imageanalysis/Annotations/RegionTextList.h>
#include <imageanalysis/IO/AsciiAnnotationFileLine.h>

#include "Region.h"
#include "RegionImportExport.h"

namespace carta {

class CrtfImportExport : public RegionImportExport {
public:
    // Import constructor
    // Use casa::RegionTextList to create casa::Annotation AnnRegions for RegionState parameters
    // file_is_filename : indicates whether file parameter contains file name or file contents.
    CrtfImportExport(std::shared_ptr<casacore::CoordinateSystem> image_coord_sys, const casacore::IPosition& image_shape, int stokes_axis,
        int file_id, const std::string& file, bool file_is_filename);

    // Export constructor
    // Creates casa::RegionTextList to which casa::AnnRegion/AnnSymbol regions are added with AddExportRegion.
    // ExportRegions prints these regions to a file or vector of strings.
    CrtfImportExport(std::shared_ptr<casacore::CoordinateSystem> image_coord_sys, const casacore::IPosition& image_shape, int stokes_axis);

    // Export regions
    // Export using RegionState pixel control points
    bool AddExportRegion(const RegionState& region_state, const RegionStyle& region_style) override;

    // Print regions to file or string vector
    bool ExportRegions(std::string& filename, std::string& error) override;
    bool ExportRegions(std::vector<std::string>& contents, std::string& error) override;

protected:
    // Export using Quantities
    bool AddExportRegion(CARTA::RegionType region_type, const RegionStyle& region_style,
        const std::vector<casacore::Quantity>& control_points, const casacore::Quantity& rotation) override;

private:
    void ProcessFileLines(std::vector<std::string>& lines);
    std::string GetRegionDirectionFrame(std::unordered_map<std::string, std::string>& properties);
    RegionState ImportAnnSymbol(std::vector<std::string>& parameters, std::string& coord_frame);
    RegionState ImportAnnBox(std::vector<std::string>& parameters, std::string& coord_frame);
    RegionState ImportAnnEllipse(std::vector<std::string>& parameters, std::string& coord_frame);
    RegionState ImportAnnPolygonLine(std::vector<std::string>& parameters, std::string& coord_frame);
    RegionStyle ImportStyleParameters(std::unordered_map<std::string, std::string>& properties);

    // Rectangle import helpers
    bool GetBoxControlPoints(std::string& box_definition, std::vector<CARTA::Point>& control_points, float& rotation);
    bool GetBoxControlPoints(
        std::vector<std::string>& parameters, std::string& region_frame, std::vector<CARTA::Point>& control_points, float& rotation);
    bool GetCenterBoxPoints(const std::string& region, casacore::Quantity& cx, casacore::Quantity& cy, casacore::Quantity& width,
        casacore::Quantity& height, std::string& region_frame, std::vector<CARTA::Point>& control_points);
    bool GetRectBoxPoints(casacore::Quantity& blcx, casacore::Quantity& blcy, casacore::Quantity& trcx, casacore::Quantity& trcy,
        std::string& region_frame, std::vector<CARTA::Point>& control_points);

    // Append style parameters to line string
    void ExportStyleParameters(const RegionStyle& region_style, std::string& region_line);
    void ExportStyleParameters(const RegionStyle& region_style, casa::AnnotationBase* region);

    // Export helpers
    // AnnRegion parameter
    casacore::Vector<casacore::Stokes::StokesTypes> GetStokesTypes();
    // Create header when printing region file
    std::string GetCrtfVersionHeader();

    // Imported globals
    std::unordered_map<std::string, std::string> _global_properties;

    // For export: add regions to list then print them
    casa::RegionTextList _region_list;

    // AnnRegion needs StokesTypes parameter; fallback if coord sys has no StokesCoordinate
    int _stokes_axis;
};

} // namespace carta

#endif // CARTA_BACKEND_REGION_CRTFIMPORTEXPORT_H_
