/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ZarrStores.h"

#include <algorithm>
#include <filesystem>
#include <limits>
#include <system_error>
#include <utility>

#include <casacore/casa/OS/File.h>
#include <spdlog/spdlog.h>

#include "CartaZarrAxes.h"
#include "ZarrContext.h"

namespace fs = std::filesystem;

namespace carta {

namespace {

// Absolute path with symlinks resolved, so that two names for one store share an entry
std::string KeyFor(const std::string& path) {
    std::error_code error;
    const auto resolved = fs::canonical(fs::path(path), error);
    return error ? path : resolved.string();
}

std::int64_t DirectoryTime(const std::string& path) {
    std::error_code error;
    const auto time = fs::last_write_time(fs::path(path), error);
    return error ? 0 : static_cast<std::int64_t>(time.time_since_epoch().count());
}

} // namespace

ZarrStores::ZarrStores(std::size_t capacity, std::chrono::seconds ttl) : _capacity(std::max<std::size_t>(capacity, 1)), _ttl(ttl) {}

ZarrStores& ZarrStores::Instance() {
    static ZarrStores stores(ZARR_STORES_SIZE, ZARR_STORES_TTL);
    return stores;
}

std::shared_ptr<const ZarrStoreLook> ZarrStores::Look(const std::string& path) {
    const auto key = KeyFor(path);
    const auto directory_mtime = DirectoryTime(key);
    {
        std::scoped_lock lock(_mutex);
        auto found = _records.find(key);
        if (found != _records.end()) {
            if ((Clock::now() - found->second.looked_at) <= _ttl && found->second.directory_mtime == directory_mtime) {
                _queue.remove(key);
                _queue.push_front(key);
                return found->second.look;
            }
            _records.erase(found);
            _queue.remove(key);
        }
    }

    // Opened without the lock, since it walks the store; if two callers open the same path, the later one is kept
    auto look = LookAt(key);

    std::scoped_lock lock(_mutex);
    if (_records.find(key) == _records.end()) {
        if (_records.size() >= _capacity) {
            _records.erase(_queue.back());
            _queue.pop_back();
        }
        _queue.push_front(key);
    }
    _records[key] = Record{directory_mtime, Clock::now(), look};
    return look;
}

std::shared_ptr<const ZarrStoreLook> ZarrStores::LookAt(const std::string& path) {
    auto look = std::make_shared<ZarrStoreLook>();
    look->path = path;
    std::error_code error;
    // Only a directory can be a store
    if (!fs::is_directory(fs::path(path), error)) {
        return look;
    }

    auto dataset = carta::zarr::Dataset::Open(GetZarrContext(), path);
    if (!dataset) {
        const auto code = dataset.error().code;
        // Nothing there, not Zarr, or not an image store
        if (code == carta::zarr::ErrorCode::not_found || code == carta::zarr::ErrorCode::not_zarr ||
            code == carta::zarr::ErrorCode::unsupported_schema) {
            return look;
        }
        // A store that would not open is a malformed image only if the XRADIO profile recognises it
        const auto probed = carta::zarr::ProbeSchema(path, carta::zarr::kXradioImageSchema);
        if (probed && probed.value().kind == carta::zarr::SchemaMatchKind::invalid) {
            look->is_zarr = true;
            look->why_not_openable = dataset.error().message;
        }
        return look;
    }

    look->is_zarr = true;
    auto offer = OfferedImages(dataset.value().descriptor());
    for (const auto& refused : offer.refused) {
        spdlog::debug("Not offering Zarr image {} in {}: {}", refused.id, path, refused.reason);
    }
    look->offered = std::move(offer.ids);
    look->why_not_openable = std::move(offer.why_none);
    look->dataset = std::move(dataset.value());
    return look;
}

ZarrStoreSize ZarrStoreLook::Size() const {
    std::scoped_lock lock(_size_mutex);
    if (_size) {
        return *_size;
    }

    // A store that will not open is not measured, since walking a large one has no time limit
    ZarrStoreSize size;
    if (dataset) {
        size.bytes = casacore::File(path).size();
        auto measured = dataset->Size();
        if (measured && measured.value().bytes <= static_cast<std::uint64_t>(std::numeric_limits<int64_t>::max())) {
            size.bytes = static_cast<int64_t>(measured.value().bytes);
            size.declared = measured.value().basis == carta::zarr::SizeBasis::declared;
        }
    }
    _size = size;
    return size;
}

void ZarrStores::Clear() {
    std::scoped_lock lock(_mutex);
    _records.clear();
    _queue.clear();
}

} // namespace carta
