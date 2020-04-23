//# RegionImportExport.h: handle region import/export in CRTF and DS9 formats

#ifndef CARTA_BACKEND_REGION_REGIONIMPORTEXPORT_H_
#define CARTA_BACKEND_REGION_REGIONIMPORTEXPORT_H_

#include <casacore/casa/Arrays/IPosition.h>
#include <casacore/coordinates/Coordinates/CoordinateSystem.h>

#include <carta-protobuf/defs.pb.h>

#include "Region.h"

namespace carta {

class RegionImportExport {
public:
    RegionImportExport() {}

    // Import constructor: file_id to add to RegionState
    RegionImportExport(const casacore::CoordinateSystem& image_coord_sys, const casacore::IPosition& image_shape, int file_id);
    // Export constructor
    RegionImportExport(const casacore::CoordinateSystem& image_coord_sys, const casacore::IPosition& image_shape);

    // Retrieve imported regions as RegionState vector
    std::vector<RegionState> GetImportedRegions(std::string& error);

    // Add region to export: RegionState for pixel coords in reference image,
    // Record for world coordinates or for either coordinate type applied to another image
    virtual bool AddExportRegion(const RegionState& region) = 0;
    virtual bool AddExportRegion(const casacore::RecordInterface& region) = 0;
    // Perform export; ostream could be for output file (ofstream) or string (ostringstream)
    virtual bool ExportRegions(std::string& filename, std::string& error) = 0;
    virtual bool ExportRegions(std::vector<std::string>& contents, std::string& error) = 0;

protected:
    // Image info to which region is applied
    casacore::CoordinateSystem _coord_sys;
    casacore::IPosition _image_shape;

    // For import
    int _file_id;
    std::string _import_errors;
    std::vector<RegionState> _regions;
};

} // namespace carta

#endif // CARTA_BACKEND_REGION_REGIONIMPORTEXPORT_H_
