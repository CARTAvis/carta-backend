/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ZarrStores.h"

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <utility>

#include <spdlog/spdlog.h>

#include "Cache/FileInfoCache.h"
#include "CartaZarrAxes.h"
#include "ZarrContext.h"

namespace fs = std::filesystem;

namespace carta {

namespace {

std::int64_t DirectoryTime(const std::string& path) {
    std::error_code error;
    const auto time = fs::last_write_time(fs::path(path), error);
    return error ? 0 : static_cast<std::int64_t>(time.time_since_epoch().count());
}

} // namespace

ZarrStores::ZarrStores(std::size_t capacity, std::chrono::seconds ttl) : _capacity(std::max<std::size_t>(capacity, 1)), _ttl(ttl) {}

ZarrStores& ZarrStores::Instance() {
    static ZarrStores stores(FILE_INFO_CACHE_SIZE, FILE_INFO_CACHE_TTL);
    return stores;
}

std::shared_ptr<const ZarrStoreLook> ZarrStores::Look(const std::string& path) {
    const auto key = FileInfoCache::KeyFor(path);
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

    // Looked at without the lock, since it walks the store: two callers asking about the same path at
    // once both look, and the second answer is kept.
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
    std::error_code error;
    // A store here is a directory; a file is never one, and is not worth asking the library about.
    if (!fs::is_directory(fs::path(path), error)) {
        return look;
    }

    auto dataset = carta::zarr::Dataset::Open(GetZarrContext(), path);
    if (!dataset) {
        const auto code = dataset.error().code;
        // Nothing there, not Zarr, or the Zarr of something else: said before the store is walked.
        if (code == carta::zarr::ErrorCode::not_found || code == carta::zarr::ErrorCode::not_zarr ||
            code == carta::zarr::ErrorCode::unsupported_schema) {
            return look;
        }
        // What is left is a store that would not open, which is a malformed image store only if the
        // profile recognised it: invalid metadata is also what a root that will not parse is called,
        // and that was never an image. Asking costs a second walk, of a store that is broken.
        const auto probed = carta::zarr::ProbeSchema(path, carta::zarr::kXradioImageSchema);
        if (probed && probed.value().kind == carta::zarr::SchemaMatchKind::invalid) {
            look->kind = ZarrStoreLook::Kind::malformed;
            look->reason = dataset.error().message;
        }
        return look;
    }

    look->kind = ZarrStoreLook::Kind::store;
    auto offer = OfferedImages(dataset.value().descriptor());
    for (const auto& refused : offer.refused) {
        spdlog::debug("Not offering Zarr image {} in {}: {}", refused.id, path, refused.reason);
    }
    look->offered = std::move(offer.ids);
    look->why_none = std::move(offer.why_none);
    look->dataset = std::move(dataset.value());
    return look;
}

void ZarrStores::Clear() {
    std::scoped_lock lock(_mutex);
    _records.clear();
    _queue.clear();
}

} // namespace carta
