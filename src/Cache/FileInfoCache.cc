/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "FileInfoCache.h"

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

using namespace carta;

namespace {

int64_t LastWriteTime(const fs::path& path) {
    std::error_code error;
    const auto time = fs::last_write_time(path, error);
    if (error) {
        return 0;
    }
    // Implementation-defined units, which is all an equality test needs.
    return static_cast<int64_t>(time.time_since_epoch().count());
}

} // namespace

bool FileInfoCache::Stamp::operator==(const Stamp& other) const {
    return directory_mtime == other.directory_mtime;
}

FileInfoCache::FileInfoCache(std::size_t capacity, std::chrono::seconds ttl) : _capacity(std::max<std::size_t>(capacity, 1)), _ttl(ttl) {}

FileInfoCache& FileInfoCache::Instance() {
    static FileInfoCache cache(FILE_INFO_CACHE_SIZE, FILE_INFO_CACHE_TTL);
    return cache;
}

std::string FileInfoCache::KeyFor(const std::string& filename) {
    std::error_code error;
    // Follows symlinks, unlike the lstat behind casacore's File::modifyTime: a store reached
    // through a link must be stamped by the store rather than by a link that never changes.
    const auto resolved = fs::canonical(fs::path(filename), error);
    if (error) {
        return filename;
    }
    return resolved.string();
}

FileInfoCache::Stamp FileInfoCache::StampFor(const std::string& key) {
    const fs::path root(key);
    Stamp stamp;
    stamp.directory_mtime = LastWriteTime(root);
    return stamp;
}

std::optional<FileInfoCache::Entry> FileInfoCache::Get(const std::string& key, const Stamp& stamp) {
    std::scoped_lock guard(_mutex);

    auto found = _map.find(key);
    if (found == _map.end()) {
        return std::nullopt;
    }

    const auto& record = found->second;
    if ((Clock::now() - record.derived_at) > _ttl || !(record.stamp == stamp)) {
        Drop(key);
        return std::nullopt;
    }

    Touch(key);
    return record.entry;
}

void FileInfoCache::Put(const std::string& key, const Stamp& stamp, Entry entry) {
    std::scoped_lock guard(_mutex);

    if (_map.find(key) == _map.end()) {
        // Evict the least recently used entry if this one does not fit.
        if (_map.size() >= _capacity) {
            _map.erase(_queue.back());
            _queue.pop_back();
        }
        _queue.push_front(key);
    } else {
        Touch(key);
    }

    _map[key] = Record{stamp, std::move(entry), Clock::now()};
}

void FileInfoCache::Clear() {
    std::scoped_lock guard(_mutex);
    _map.clear();
    _queue.clear();
}

void FileInfoCache::Drop(const std::string& key) {
    _map.erase(key);
    _queue.remove(key);
}

void FileInfoCache::Touch(const std::string& key) {
    _queue.remove(key);
    _queue.push_front(key);
}
