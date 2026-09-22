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

#include "Util/Casacore.h"
#include "Util/File.h"
#include "ImageData/ZarrContext.h"

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
        file_info.set_size(entry->size);
        file_info.set_size_is_upper_bound(entry->size_is_upper_bound);
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
    file_info.set_size_is_upper_bound(false);

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

    const auto context = GetZarrContext();
    auto dataset = carta::zarr::Dataset::Open(*context, _filename);
    if (!dataset) {
        casacore::Directory cc_dir(cc_file);
        entry.size = cc_dir.size();
        return entry;
    }

    entry.size = cc_file.size();
    auto dataset_size = dataset.value().Size();
    if (dataset_size && dataset_size.value().bytes <= static_cast<std::uint64_t>(std::numeric_limits<int64_t>::max())) {
        entry.size = static_cast<int64_t>(dataset_size.value().bytes);
        // The library says which of two questions it answered, not how the answers compare -- see
        // its ADR 0008. Treating the declared size as an upper bound is this line's inference, and
        // it holds for the reason the walk usually gives up: a store too large to enumerate inside
        // the timeout is one whose compressed chunks dwarf its metadata. It does not hold when the
        // walk failed for some other reason, such as a directory that refused to be read, and
        // nothing here can tell the two apart.
        entry.size_is_upper_bound = dataset_size.value().basis == carta::zarr::SizeBasis::declared;
    }

    const auto& images = dataset.value().descriptor().images;
    for (const auto& image : images) {
        if (image.readable) {
            entry.hdu_list.push_back(image.id);
        }
    }
    entry.complete = !images.empty();

    return entry;
}

CARTA::FileType FileInfoLoader::GetCartaFileType(const string& filename) {
    // get casacore image type then convert to carta file type
    if (IsCompressedFits(filename)) {
        return CARTA::FileType::FITS;
    }
    if (IsZarr(filename)) {
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
