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

    // Import: boolean indicates whether file string is file name or file contents
    RegionImportExport(const std::string& region_file, CARTA::FileType type, const casacore::CoordinateSystem& image_coord_sys,
        const casacore::IPosition& image_shape, bool file_is_filename = true);

    std::vector<RegionState> GetImportedRegions(int file_id, std::string& error);

private:
    // Region file info
    CARTA::FileType _file_type;
    std::string _filename;
    std::string _contents;

    // Image info to which region is applied
    casacore::CoordinateSystem _coord_sys;
    casacore::IPosition _image_shape;
};

} // namespace carta

#endif // CARTA_BACKEND_REGION_REGIONIMPORTEXPORT_H_
