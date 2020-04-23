#include "RegionImportExport.h"

#include <casacore/coordinates/Coordinates/DirectionCoordinate.h>

using namespace carta;

RegionImportExport::RegionImportExport(
    const casacore::CoordinateSystem& image_coord_sys, const casacore::IPosition& image_shape, int file_id)
    : _coord_sys(image_coord_sys), _image_shape(image_shape), _file_id(file_id) {
    // Constructor for import. Use GetImportedRegions to retrieve regions.
}

RegionImportExport::RegionImportExport(const casacore::CoordinateSystem& image_coord_sys, const casacore::IPosition& image_shape)
    : _coord_sys(image_coord_sys), _image_shape(image_shape) {
    // Constructor for export. Use AddExportRegion to add regions, then ExportRegions to finalize
}

// public accessors

std::vector<RegionState> RegionImportExport::GetImportedRegions(std::string& error) {
    // Parse the file in the constructor to create RegionStates with given reference file_id; return any errors in error
    error = _import_errors;
    if ((_regions.size() == 0) && error.empty()) {
        error = "Import error: zero regions set.";
    }

    return _regions;
}
