/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "LoaderCache.h"

#include "Logger/Logger.h"

using namespace carta;

LoaderCache::LoaderCache(int capacity) : _capacity(capacity){};

std::shared_ptr<FileLoader> LoaderCache::Get(const std::string& filename, const std::string& directory) {
    std::unique_lock<std::mutex> guard(_loader_cache_mutex);
    auto key = GetKey(filename, directory);

    // We have a cached loader, but the file has changed
    if ((_map.find(key) != _map.end()) && _map[key] && _map[key]->ImageUpdated()) {
        _map.erase(key);
        _queue.remove(key);
    }

    // We don't have a cached loader
    if (_map.find(key) == _map.end()) {
        // Create the loader -- don't block while doing this
        std::shared_ptr<FileLoader> loader_ptr;
        guard.unlock();
        loader_ptr = std::shared_ptr<FileLoader>(FileLoader::GetLoader(filename, directory));
        guard.lock();

        // Check if the loader was added in the meantime
        if (_map.find(key) == _map.end()) {
            // Evict oldest loader if necessary
            if (_map.size() == _capacity) {
                _map.erase(_queue.back());
                _queue.pop_back();
            }

            // Insert the new loader
            _map[key] = loader_ptr;
            _queue.push_front(key);
        }
    } else {
        // Touch the cache entry
        _queue.remove(key);
        _queue.push_front(key);
    }

    return _map[key];
}

void LoaderCache::Remove(const std::string& filename, const std::string& directory) {
    std::unique_lock<std::mutex> guard(_loader_cache_mutex);
    auto key = GetKey(filename, directory);
    _map.erase(key);
    _queue.remove(key);
}

std::string LoaderCache::GetKey(const std::string& filename, const std::string& directory) {
    return (directory.empty() ? filename : fmt::format("{}/{}", directory, filename));
}
