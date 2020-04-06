//# CrtfImportExport.h: handle CRTF region file import and export

#ifndef CARTA_BACKEND_REGION_CRTFIMPORTEXPORT_H_
#define CARTA_BACKEND_REGION_CRTFIMPORTEXPORT_H_

#include <casacore/casa/Arrays/IPosition.h>
#include <casacore/coordinates/Coordinates/CoordinateSystem.h>
#include <imageanalysis/Annotations/AnnotationBase.h>
#include <imageanalysis/IO/AsciiAnnotationFileLine.h>

#include "Region.h"

namespace carta {

class CrtfImportExport {
public:
    CrtfImportExport() {}

    // constructors for import
    CrtfImportExport(
        std::string& filename, const casacore::CoordinateSystem& image_coord_sys, casacore::IPosition& image_shape, int file_id);
    CrtfImportExport(
        const casacore::CoordinateSystem& image_coord_sys, std::string& contents, casacore::IPosition& image_shape, int file_id);

    // constructor for export
    // CrtfImportExport(const casacore::CoordinateSystem& image_coord_sys, bool pixel_coord);

    std::vector<RegionState> GetImportedRegions(std::string& error);

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
    void AddImportError(std::string& error);

    /*
    // export regions
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

    // Image info to import region to
    casacore::CoordinateSystem _coord_sys;
    casacore::IPosition _image_shape;

    // Output of import, or input to export
    std::vector<RegionState> _regions;

    // For import
    int _file_id; // to add to RegionState
    std::string _import_errors;
};

} // namespace carta

#endif // CARTA_BACKEND_REGION_CRTFIMPORTEXPORT_H_
