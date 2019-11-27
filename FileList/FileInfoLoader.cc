//# FileInfoLoader.cc: fill FileInfo for all supported file types

#include "FileInfoLoader.h"

#include <casacore/casa/HDF5/HDF5File.h>
#include <casacore/casa/HDF5/HDF5Group.h>
#include <casacore/casa/OS/Directory.h>
#include <casacore/casa/OS/File.h>

#include "../Util.h"
#include "FitsHduList.h"

FileInfoLoader::FileInfoLoader(const std::string& filename) : _filename(filename) {
    _type = GetCartaFileType(filename);
}

bool FileInfoLoader::FillFileInfo(CARTA::FileInfo* file_info) {
    // Fill FileInfo submessage with type, size, hdus
    casacore::File cc_file(_filename);
    if (!cc_file.exists()) {
        return false;
    }
    std::string filename_only = cc_file.path().baseName();
    file_info->set_name(filename_only);

    // fill FileInfo submessage
    int64_t file_size(cc_file.size());
    if (cc_file.isDirectory()) { // symlinked dirs are dirs
        casacore::Directory cc_dir(cc_file);
        file_size = cc_dir.size();
    } else if (cc_file.isSymLink()) { // gets size of link not file
        casacore::String resolved_file_name(cc_file.path().resolvedName());
        casacore::File linked_file(resolved_file_name);
        file_size = linked_file.size();
    }

    file_info->set_size(file_size);
    file_info->set_type(_type);
    // add hdu for FITS, HDF5
    if (_type == CARTA::FileType::FITS) {
        casacore::String abs_file_name(cc_file.path().absoluteName());
        return GetFitsHduList(file_info, abs_file_name);
    } else if (_type == CARTA::FileType::HDF5) {
        casacore::String abs_file_name(cc_file.path().absoluteName());
        return GetHdf5HduList(file_info, abs_file_name);
    } else {
        file_info->add_hdu_list("");
        return true;
    }
}

bool FileInfoLoader::GetFitsHduList(CARTA::FileInfo* file_info, const std::string& filename) {
    FitsHduList fits_hdu_list = FitsHduList(filename);
    return fits_hdu_list.GetHduList(file_info);
}

bool FileInfoLoader::GetHdf5HduList(CARTA::FileInfo* file_info, const std::string& filename) {
    // fill FileInfo hdu list for Hdf5
    casacore::HDF5File hdf_file(filename);
    std::vector<casacore::String> hdus(casacore::HDF5Group::linkNames(hdf_file));
    if (hdus.empty()) {
        file_info->add_hdu_list("");
    } else {
        for (auto group_name : hdus) {
            file_info->add_hdu_list(group_name);
        }
    }
    return true;
}
