//# RegionImporter.h: parse region file to get CARTA::RegionState for each defined region

#ifndef CARTA_BACKEND_REGION_REGIONIMPORTER_H_
#define CARTA_BACKEND_REGION_REGIONIMPORTER_H_

#include <casacore/coordinates/Coordinates/CoordinateSystem.h>

#include "Region.h"

namespace carta {

struct Ds9Properties {
    std::string text;
    std::string color = "green";
    std::string font = "helvetica 10 normal roman";
    bool select_region = true;
    bool edit_region = true;
    bool move_region = true;
    bool delete_region = true;
    bool highlite_region = true;
    bool include_region = true;
    bool fixed_region = false;
};

class RegionImporter {
public:
    RegionImporter() {}
    // boolean indicates whether file string is file name or file contents
    RegionImporter(const std::string& region_file, CARTA::FileType type, const casacore::CoordinateSystem& image_coord_sys,
        const casacore::IPosition& image_shape, bool file_is_filename = true);

    std::vector<RegionState> GetRegions(int file_id, std::string& error);

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

#endif // CARTA_BACKEND_REGION_REGIONIMPORTER_H_
