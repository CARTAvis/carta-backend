#include "RegionImportExport.h"

#include "CrtfImportExport.h"
#include "Ds9ImportExport.h"

using namespace carta;

RegionImportExport::RegionImportExport(const std::string& region_file, CARTA::FileType type,
    const casacore::CoordinateSystem& image_coord_sys, const casacore::IPosition& image_shape, bool file_is_filename)
    : _file_type(type), _coord_sys(image_coord_sys), _image_shape(image_shape) {
    if (file_is_filename) {
        _filename = region_file;
    } else {
        _contents = region_file;
    }
}

// public accessors

std::vector<RegionState> RegionImportExport::GetImportedRegions(int file_id, std::string& error) {
    // Parse the file in the constructor to create RegionStates with given reference file_id; return any errors in error
    std::vector<RegionState> regions;
    std::string error_prefix("Import region failed: ");

    try {
        switch (_file_type) {
            case CARTA::FileType::CRTF: {
                if (!_coord_sys.hasDirectionCoordinate()) {
                    // limitation of imageanalysis
                    error = error_prefix + "image coordinate system has no direction coordinate.";
                    return regions;
                }

                CrtfImportExport crtf_importer;
                if (_filename.empty()) {
                    crtf_importer = CrtfImportExport(_coord_sys, _contents, _image_shape, file_id);
                } else {
                    crtf_importer = CrtfImportExport(_filename, _coord_sys, _image_shape, file_id);
                }

                // Get vector of RegionState, and any error messages
                regions = crtf_importer.GetImportedRegions(error);
                break;
            }
            case CARTA::FileType::REG: {
                Ds9ImportExport ds9_importer;
                if (_filename.empty()) {
                    ds9_importer = Ds9ImportExport(_coord_sys, _contents, _image_shape, file_id);
                } else {
                    ds9_importer = Ds9ImportExport(_filename, _coord_sys, _image_shape, file_id);
                }

                // Get vector of RegionState, and any error messages
                regions = ds9_importer.GetImportedRegions(error);
                break;
            }
            default: {
                error = error_prefix + "file type not supported.";
                break;
            }
        }
    } catch (casacore::AipsError& err) {
        casacore::String error_message(err.getMesg());
        std::cerr << error_prefix << error_message << std::endl;

        // shorten error message to user
        error_message = error_message.before("... thrown by"); // remove casacode file
        error_message = error_message.before(" at File");      // remove casacode file
        std::ostringstream oss;
        oss << error_prefix << error_message;
        error = oss.str();
    }

    if ((regions.size() == 0) && error.empty()) {
        error = error_prefix + "zero regions set";
    }
}
