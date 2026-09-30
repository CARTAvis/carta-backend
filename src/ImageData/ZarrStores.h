/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_ZARRSTORES_H_
#define CARTA_SRC_IMAGEDATA_ZARRSTORES_H_

#include <chrono>
#include <cstddef>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <carta-zarr/carta_zarr.h>

namespace carta {

// What a path turned out to be, as far as Zarr goes.
struct ZarrStoreLook {
    enum class Kind {
        // Not a Zarr image store: a plain folder, a file, or a Zarr store of something else.
        none,
        // An image store, opened.
        store,
        // A store the XRADIO profile recognised and found broken. It is shown as an image, since that
        // is what it was meant to be, and opening it says why it will not open (ADR 0009 in carta-zarr).
        malformed,
    };

    Kind kind = Kind::none;
    // Why a malformed store will not open.
    std::string reason;
    // The opened store, for a store.
    std::optional<carta::zarr::Dataset> dataset;
    // The images CARTA offers, the one it opens by default first; and why there are none, when there
    // are none.
    std::vector<std::string> offered;
    std::string why_none;

    bool IsZarr() const {
        return kind != Kind::none;
    }
};

// The Zarr stores this process has looked at, each looked at once for a while.
//
// Whether a path is a Zarr image store, and what it offers, is asked by the file list, by the file
// info, by the choice of loader and by the image when it opens -- and each answer opens the store,
// since the library learns what a store holds by listing every node in it and reading each node's
// metadata. It does not go below an array, so this costs what the store's nodes do, not what its
// chunks do; what walks the chunks is the store's size, which the file info keeps. So a path is looked
// at once, by opening it, and the opened store is what every one of those questions is answered from,
// the image's included.
//
// Staleness is bounded by time, as the file info's is: an answer is kept for a while unless the
// directory's own timestamp moves, and what an opened store says it holds is a snapshot in any case
// (ADR 0010 in carta-zarr). Nothing here reads a store's files itself; where its metadata lives is the
// library's business (ADR 0004).
class ZarrStores {
public:
    ZarrStores(std::size_t capacity, std::chrono::seconds ttl);

    // The one every caller shares.
    static ZarrStores& Instance();

    std::shared_ptr<const ZarrStoreLook> Look(const std::string& path);

    // Forget everything. For tests, which cannot otherwise tell one run from the next.
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
