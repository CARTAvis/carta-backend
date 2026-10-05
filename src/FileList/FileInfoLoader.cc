/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# FileInfoLoader.cc: fill FileInfo for all supported file types

#include "FileInfoLoader.h"

#include <limits>
#include <optional>

#include <casacore/casa/HDF5/HDF5File.h>
#include <casacore/casa/HDF5/HDF5Group.h>
#include <casacore/casa/OS/Directory.h>
#include <casacore/casa/OS/File.h>

#include <carta-zarr/carta_zarr.h>
#include <spdlog/spdlog.h>

#include "Util/Casacore.h"
#include "Util/File.h"
#include "ImageData/CartaZarrAxes.h"
#include "ImageData/ZarrContext.h"
#include "ImageData/ZarrStores.h"

using namespace carta;

// Deciding the type probes the file, and for a directory image FillFileInfo may have the answer
// cached already, so the probe waits until something actually needs it.
FileInfoLoader::FileInfoLoader(const std::string& filename) : _filename(filename), _type(CARTA::FileType::UNKNOWN), _type_known(false) {}

FileInfoLoader::FileInfoLoader(const std::string& filename, const CARTA::FileType& type)
    : _filename(filename), _type(type), _type_known(true) {}

CARTA::FileType FileInfoLoader::ResolvedType() {
    if (!_type_known) {
        _type = GetCartaFileType(_filename);
        _type_known = true;
    }
    return _type;
}

bool FileInfoLoader::FillFileInfo(CARTA::FileInfo& file_info) {
    // Fill FileInfo submessage with type, size, hdus
    casacore::File cc_file(_filename);
    if (!cc_file.exists()) {
        return false;
    }

    if (file_info.name().empty()) {
        // set resolved filename; if symlink, set in calling function
        std::string filename_only = cc_file.path().baseName();
        file_info.set_name(filename_only);
    }

    file_info.set_date(cc_file.modifyTime());

    if (cc_file.isDirectory()) { // symlinked dirs are dirs
        // The size of a directory image is a walk of it, and for a Zarr store a probe as well --
        // and the probe is also how the type is decided. All of it is derived once for the folder
        // listing, again when the file is selected, and again when it is opened, so the cache
        // stands in front of all three. Nothing above this point needs the type, which is why a
        // hit here costs no probe at all.
        auto& cache = FileInfoCache::Instance();
        const auto key = FileInfoCache::KeyFor(_filename);
        const auto stamp = FileInfoCache::StampFor(key);

        auto entry = cache.Get(key, stamp);
        if (!entry) {
            entry = FillDirectoryInfo();
            cache.Put(key, stamp, *entry);
        }

        file_info.set_type(entry->type);
        _message = entry->message;
        file_info.set_size(entry->size);
        file_info.set_size_is_declared(entry->size_is_declared);
        for (const auto& hdu : entry->hdu_list) {
            file_info.add_hdu_list(hdu);
        }
        return entry->complete;
    }

    const auto type = ResolvedType();
    file_info.set_type(type);

    int64_t file_size(cc_file.size());
    if (cc_file.isSymLink()) { // gets size of link not file
        casacore::String resolved_filename(cc_file.path().resolvedName());
        casacore::File linked_file(resolved_filename);
        file_size = linked_file.size();
    }
    file_info.set_size(file_size);
    file_info.set_size_is_declared(false);

    if (type == CARTA::FileType::ZARR) {
        // A Zarr image is a directory; a file carrying the type has no images to list.
        return false;
    }

    // add hdu for HDF5
    if (type == CARTA::FileType::HDF5) {
        casacore::String abs_file_name(cc_file.path().absoluteName());
        return GetHdf5HduList(file_info, abs_file_name);
    }

    file_info.add_hdu_list("");
    return true;
}

FileInfoCache::Entry FileInfoLoader::FillDirectoryInfo() {
    FileInfoCache::Entry entry;
    casacore::File cc_file(_filename);
    entry.type = ResolvedType();

    if (entry.type != CARTA::FileType::ZARR) {
        casacore::Directory cc_dir(cc_file);
        entry.size = cc_dir.size();
        entry.hdu_list.emplace_back("");
        entry.complete = true;
        return entry;
    }

    const auto look = ZarrStores::Instance().Look(_filename);
    if (!look->dataset) {
        // A store that will not open has no images to list, and the reason goes back with the answer
        // for whoever selected it. Opening it says the same.
        casacore::Directory cc_dir(cc_file);
        entry.size = cc_dir.size();
        entry.message = look->reason;
        return entry;
    }
    const auto& dataset = look->dataset;

    entry.size = cc_file.size();
    auto dataset_size = dataset->Size();
    if (dataset_size && dataset_size.value().bytes <= static_cast<std::uint64_t>(std::numeric_limits<int64_t>::max())) {
        entry.size = static_cast<int64_t>(dataset_size.value().bytes);
        // The library says which of two questions it answered, and that is what is passed on: the
        // bytes the store occupies, or the uncompressed size its metadata declares when those could
        // not be measured. Not how the two compare, which nothing here knows -- see its ADR 0008.
        entry.size_is_declared = dataset_size.value().basis == carta::zarr::SizeBasis::declared;
    }

    // Offered only if CARTA will open it, which is more than the library promises with openable: the
    // library opens an image with two times, and CARTA displays one. The same offer is what
    // CartaZarrImage takes its default image from.
    entry.hdu_list = look->offered;
    entry.complete = !dataset->descriptor().images.empty();
    if (entry.hdu_list.empty()) {
        entry.message = look->why_none;
    }

    return entry;
}

CARTA::FileType FileInfoLoader::GetCartaFileType(const string& filename) {
    // get casacore image type then convert to carta file type
    if (IsCompressedFits(filename)) {
        return CARTA::FileType::FITS;
    }
    if (ZarrStores::Instance().Look(filename)->IsZarr()) {
        return CARTA::FileType::ZARR;
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
