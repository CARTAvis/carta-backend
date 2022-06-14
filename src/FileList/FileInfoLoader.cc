/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018 - 2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# FileInfoLoader.cc: fill FileInfo for all supported file types

#include "FileInfoLoader.h"

#include <casacore/casa/HDF5/HDF5File.h>
#include <casacore/casa/HDF5/HDF5Group.h>
#include <casacore/casa/OS/Directory.h>
#include <casacore/casa/OS/File.h>

#include "Util/Casacore.h"
#include "Util/File.h"

using namespace carta;

FileInfoLoader::FileInfoLoader(const std::string& filename) : _filename(filename) {
    _type = GetCartaFileType(filename);
}

FileInfoLoader::FileInfoLoader(const std::string& filename, const CARTA::FileType& type) : _filename(filename), _type(type) {}

bool FileInfoLoader::FillFileInfo(CARTA::FileInfo& file_info) {
    // Fill FileInfo submessage with type, size, hdus
    bool success(false);
    casacore::File cc_file(_filename);
    if (!cc_file.exists()) {
        return success;
    }

    if (file_info.name().empty()) {
        // set resolved filename; if symlink, set in calling function
        std::string filename_only = cc_file.path().baseName();
        file_info.set_name(filename_only);
    }

    // fill FileInfo submessage
    int64_t file_size(cc_file.size());
    if (cc_file.isDirectory()) { // symlinked dirs are dirs
        casacore::Directory cc_dir(cc_file);
        file_size = cc_dir.size();
    } else if (cc_file.isSymLink()) { // gets size of link not file
        casacore::String resolved_filename(cc_file.path().resolvedName());
        casacore::File linked_file(resolved_filename);
        file_size = linked_file.size();
    }

    file_info.set_date(cc_file.modifyTime());
    file_info.set_size(file_size);
    file_info.set_type(_type);

    // add hdu for HDF5
    if (_type == CARTA::FileType::HDF5) {
        casacore::String abs_file_name(cc_file.path().absoluteName());
        success = GetHdf5HduList(file_info, abs_file_name);
    } else {
        file_info.add_hdu_list("");
        success = true;
    }

    return success;
}

CARTA::FileType FileInfoLoader::GetCartaFileType(const string& filename) {
    // get casacore image type then convert to carta file type
    if (IsCompressedFits(filename)) {
        return CARTA::FileType::FITS;
    }

    switch (CasacoreImageType(filename)) {
        case casacore::ImageOpener::AIPSPP:
        case casacore::ImageOpener::IMAGECONCAT:
        case casacore::ImageOpener::IMAGEEXPR:
        case casacore::ImageOpener::COMPLISTIMAGE:
            return CARTA::FileType::CASA;
        case casacore::ImageOpener::FITS:
            return CARTA::FileType::FITS;
        case casacore::ImageOpener::MIRIAD:
            return CARTA::FileType::MIRIAD;
        case casacore::ImageOpener::HDF5:
            return CARTA::FileType::HDF5;
        case casacore::ImageOpener::GIPSY:
        case casacore::ImageOpener::CAIPS:
        case casacore::ImageOpener::NEWSTAR:
        default:
            return CARTA::FileType::UNKNOWN;
    }
}

bool FileInfoLoader::GetHdf5HduList(CARTA::FileInfo& file_info, const std::string& filename) {
    // fill FileInfo hdu list for Hdf5
    casacore::HDF5File hdf_file(filename);
    std::vector<casacore::String> hdus(casacore::HDF5Group::linkNames(hdf_file));
    if (hdus.empty()) {
        file_info.add_hdu_list("");
    } else {
        for (auto group_name : hdus) {
            file_info.add_hdu_list(group_name);
        }
    }
    return true;
}
