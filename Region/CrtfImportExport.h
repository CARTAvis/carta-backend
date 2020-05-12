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
    // Use casa::RegionTextList to parse file and create casa::Annotation AnnRegions
    // which are converted to RegionState (pixel coords) to set Region
    CrtfImportExport(casacore::CoordinateSystem* image_coord_sys, const casacore::IPosition& image_shape, int file_id,
        const std::string& file, bool file_is_filename);

    // Export constructor
    // Set up casa::RegionTextList; when regions are added, a casa::Annotation
    // AnnRegion is created and added to the list, which is then "print"-ed to
    // a file or vector of strings
    CrtfImportExport(casacore::CoordinateSystem* image_coord_sys, const casacore::IPosition& image_shape);

    ~CrtfImportExport();

    // Export regions
    // Create AnnRegion and add to RegionTextList
    bool AddExportRegion(const RegionState& region_state) override;
    // Print regions to file or string vector
    bool ExportRegions(std::string& filename, std::string& error) override;
    bool ExportRegions(std::vector<std::string>& contents, std::string& error) override;

protected:
    // Create AnnRegion and add to RegionTextList
    bool AddExportRegion(const std::string& name, CARTA::RegionType type, const std::vector<casacore::Quantity>& control_points,
        const casacore::Quantity& rotation) override;

private:
    // Import Annotation regions to RegionState vector
    void ImportAnnotationFileLine(casa::AsciiAnnotationFileLine& file_line);
    void ImportAnnSymbol(casacore::CountedPtr<const casa::AnnotationBase>& annotation_region);
    void ImportAnnBox(casacore::CountedPtr<const casa::AnnotationBase>& annotation_region);
    void ImportAnnRotBox(casacore::CountedPtr<const casa::AnnotationBase>& annotation_region);
    void ImportAnnPolygon(casacore::CountedPtr<const casa::AnnotationBase>& annotation_region);
    void ImportAnnEllipse(casacore::CountedPtr<const casa::AnnotationBase>& annotation_region);
    void RectangleControlPointsFromVertices(
        std::vector<casacore::Double>& x, std::vector<casacore::Double>& y, std::vector<CARTA::Point>& control_points);
    casacore::Vector<casacore::Stokes::StokesTypes> GetStokesTypes();
    double AngleToPixelLength(casacore::Quantity angle, unsigned int pixel_axis);

    // For export: add regions to list then print them
    casa::RegionTextList _region_list;
};

} // namespace carta

#endif // CARTA_BACKEND_REGION_CRTFIMPORTEXPORT_H_
