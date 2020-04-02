//# RegionImporter: parses lines of input region file into CARTA::RegionState for import

#include "RegionImporter.h"

using namespace carta;

RegionImporter::RegionImporter(const std::string& region_file, CARTA::FileType type, const casacore::CoordinateSystem& image_coord_sys,
    const casacore::IPosition& image_shape, bool file_is_filename)
    : _file_type(type), _coord_sys(image_coord_sys), _image_shape(image_shape) {
    if (file_is_filename) {
        _filename = region_file;
    } else {
        _contents = region_file;
    }
}

// public accessors

std::vector<RegionState> RegionImporter::GetRegions(int file_id, std::string& error) {
    // Parse the file in the constructor to create RegionStates with given reference file_id; return any errors in error
    std::vector<RegionState> regions;

    // TODO: parse file and create regions
    error = "Not implemented yet";
    return regions;
}
