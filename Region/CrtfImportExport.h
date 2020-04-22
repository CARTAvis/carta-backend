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

    // Import
    CrtfImportExport(const casacore::CoordinateSystem& image_coord_sys, const casacore::IPosition& image_shape, int file_id,
        const std::string& file, bool file_is_filename);

    // Export
    CrtfImportExport(const casacore::CoordinateSystem& image_coord_sys, const casacore::IPosition& image_shape);

private:
    // Import regions
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

    // Export regions: add each region to region list
    casa::RegionTextList _region_list;

    /*
    void AddRegion(const std::string& name, CARTA::RegionType type, const std::vector<casacore::Quantity>& control_points, float rotation);
    inline unsigned int NumRegions() {
        return _regions.size();
    }
    void PrintHeader(std::ostream& os);
    void PrintRegion(unsigned int i, std::ostream& os);
    void PrintRegionsToFile(std::ofstream& ofs);
    // region export
    void PrintBoxRegion(const RegionProperties& properties, std::ostream& os);
    void PrintEllipseRegion(const RegionProperties& properties, std::ostream& os);
    void PrintPointRegion(const RegionProperties& properties, std::ostream& os);
    void PrintPolygonRegion(const RegionProperties& properties, std::ostream& os);
    */
};

} // namespace carta

#endif // CARTA_BACKEND_REGION_CRTFIMPORTEXPORT_H_
