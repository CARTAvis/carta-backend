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
    CrtfImportExport() {}

    // Import constructor
    // Use casa::RegionTextList to create casa::Annotation AnnRegions for RegionState parameters
    // file_is_filename : indicates whether file parameter contains file name or file contents.
    CrtfImportExport(casacore::CoordinateSystem* image_coord_sys, const casacore::IPosition& image_shape, int stokes_axis, int file_id,
        const std::string& file, bool file_is_filename);

    // Export constructor
    // Creates casa::RegionTextList to which casa::AnnRegion/AnnSymbol regions are added with AddExportRegion.
    // ExportRegions prints these regions to a file or vector of strings.
    CrtfImportExport(casacore::CoordinateSystem* image_coord_sys, const casacore::IPosition& image_shape, int stokes_axis);

    ~CrtfImportExport() override;

    // Export regions
    // Create AnnRegion and add to RegionTextList
    bool AddExportRegion(const RegionState& region_state) override;
    // Print regions to file or string vector
    bool ExportRegions(std::string& filename, std::string& error) override;
    bool ExportRegions(std::vector<std::string>& contents, std::string& error) override;

protected:
    bool AddExportRegion(const std::string& name, CARTA::RegionType type, const std::vector<casacore::Quantity>& control_points,
        const casacore::Quantity& rotation) override;

private:
    // Import RegionTextList Annotation regions to RegionState vector
    void ImportAnnotationFileLine(casa::AsciiAnnotationFileLine& file_line);
    void ImportAnnSymbol(casacore::CountedPtr<const casa::AnnotationBase>& annotation_region);
    void ImportAnnBox(casacore::CountedPtr<const casa::AnnotationBase>& annotation_region);
    void ImportAnnRotBox(casacore::CountedPtr<const casa::AnnotationBase>& annotation_region);
    void ImportAnnPolygon(casacore::CountedPtr<const casa::AnnotationBase>& annotation_region);
    void ImportAnnEllipse(casacore::CountedPtr<const casa::AnnotationBase>& annotation_region);

    // Manual region import to RegionState
    // (workaround for imageanalysis RegionTextList exception for linear coord sys)
    void ProcessFileLines(std::vector<std::string>& lines);
    void ImportAnnSymbol(std::vector<std::string>& parameters, std::string& region_name);
    void ImportAnnBox(std::vector<std::string>& parameters, std::string& region_name);
    void ImportAnnEllipse(std::vector<std::string>& parameters, std::string& region_name);
    void ImportAnnPolygon(std::vector<std::string>& parameters, std::string& region_name);

    // Rectangle import helpers
    // Convert AnnPolygon pixel vertices to CARTA Control Points (center and size)
    bool RectangleControlPointsFromVertices(
        std::vector<casacore::Double>& x, std::vector<casacore::Double>& y, std::vector<CARTA::Point>& control_points);
    // Determine control points from box parameters
    bool GetBoxControlPoints(std::string& box_definition, std::vector<CARTA::Point>& control_points, float& rotation);
    bool GetBoxControlPoints(
        std::vector<std::string>& parameters, std::string& region_frame, std::vector<CARTA::Point>& control_points, float& rotation);
    bool GetCenterBoxPoints(const std::string& region, casacore::Quantity& cx, casacore::Quantity& cy, casacore::Quantity& width,
        casacore::Quantity& height, std::string& region_frame, std::vector<CARTA::Point>& control_points);
    bool GetRectBoxPoints(casacore::Quantity& blcx, casacore::Quantity& blcy, casacore::Quantity& trcx, casacore::Quantity& trcy,
        std::string& region_frame, std::vector<CARTA::Point>& control_points);

    // Export RegionState as Annotation region or string
    bool AddExportAnnotationRegion(const RegionState& region_state);
    bool AddExportRegionLine(const RegionState& region_state);
    void AddExportRegionKeywords(std::string& line, const std::string& region_name);

    // Export helpers
    // AnnRegion parameter
    casacore::Vector<casacore::Stokes::StokesTypes> GetStokesTypes();
    // Create header when printing region file
    std::string GetCrtfVersionHeader();

    // For export: add regions to list then print them
    casa::RegionTextList _region_list;

    // AnnRegion needs StokesTypes parameter; fallback if coord sys has no StokesCoordinate
    int _stokes_axis;
};

} // namespace carta

#endif // CARTA_BACKEND_REGION_CRTFIMPORTEXPORT_H_
