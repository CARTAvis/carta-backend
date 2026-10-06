/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_ZARRSTORES_H_
#define CARTA_SRC_IMAGEDATA_ZARRSTORES_H_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <carta-zarr/carta_zarr.h>

namespace carta {

constexpr std::chrono::seconds ZARR_STORES_TTL(30);
constexpr std::size_t ZARR_STORES_SIZE = 512;

struct ZarrStoreSize {
    int64_t bytes = 0;
    // The uncompressed size declared in the metadata, because the bytes on disk could not be measured
    bool declared = false;
};

// What a path is, as far as Zarr goes
struct ZarrStoreLook {
    // A Zarr image store: one that opened, or an XRADIO one that will not open but is still listed as an
    // image so that opening it says why. Not a plain folder, a file, or a Zarr store of something else.
    bool is_zarr = false;
    // Set when the store opened
    std::optional<carta::zarr::Dataset> dataset;
    // The images CARTA offers, the default first
    std::vector<std::string> offered;
    // Why no image can be opened: the store will not open, or it offers none
    std::string why_not_openable;

    // The size of the store, measured on first use and then kept; zero for a store that will not open
    ZarrStoreSize Size() const;

    std::string path;

private:
    mutable std::mutex _size_mutex;
    mutable std::optional<ZarrStoreSize> _size;
};

// The Zarr stores this process has opened, kept for ZARR_STORES_TTL. The file list, the file info, the
// choice of loader and the image all ask whether a path is a Zarr image store, what it offers and how
// large it is, so each path is opened and sized once and shared.
//
// An entry also expires when the directory's own timestamp changes. That only catches changes to its
// entry list, not writes deeper in the store.
class ZarrStores {
public:
    ZarrStores(std::size_t capacity, std::chrono::seconds ttl);

    // Shared by all sessions
    static ZarrStores& Instance();

    std::shared_ptr<const ZarrStoreLook> Look(const std::string& path);

    // For tests
    void Clear();

private:
    using Clock = std::chrono::steady_clock;

    struct Record {
        std::int64_t directory_mtime = 0;
        Clock::time_point looked_at;
        std::shared_ptr<const ZarrStoreLook> look;
    };

    static std::shared_ptr<const ZarrStoreLook> LookAt(const std::string& path);

    const std::size_t _capacity;
    const std::chrono::seconds _ttl;
    std::mutex _mutex;
    std::unordered_map<std::string, Record> _records;
    std::list<std::string> _queue; // most recently used first
};

} // namespace carta

#endif // CARTA_SRC_IMAGEDATA_ZARRSTORES_H_
