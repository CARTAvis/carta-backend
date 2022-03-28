/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "LoaderCache.h"

using namespace carta;

LoaderCache::LoaderCache(int capacity) : _capacity(capacity){};

std::shared_ptr<FileLoader<float>> LoaderCache::Get(const std::string& filename, const std::string& directory) {
    std::unique_lock<std::mutex> guard(_loader_cache_mutex);

    // We have a cached loader, but the file has changed
    if (_map.find(filename) != _map.end() && _map[filename]->ImageUpdated()) {
        _map.erase(filename);
        _queue.remove(filename);
    }

    // We don't have a cached loader
    if (_map.find(filename) == _map.end()) {
        // Create the loader -- don't block while doing this
        std::shared_ptr<FileLoader<float>> loader_ptr;
        guard.unlock();
        loader_ptr = std::shared_ptr<FileLoader<float>>(FileLoader<float>::GetLoader(filename, directory));
        guard.lock();

        // Check if the loader was added in the meantime
        if (_map.find(filename) == _map.end()) {
            // Evict oldest loader if necessary
            if (_map.size() == _capacity) {
                _map.erase(_queue.back());
                _queue.pop_back();
            }

            // Insert the new loader
            _map[filename] = loader_ptr;
            _queue.push_front(filename);
        }
    } else {
        // Touch the cache entry
        _queue.remove(filename);
        _queue.push_front(filename);
    }

    return _map[filename];
}

void LoaderCache::Remove(const std::string& filename) {
    std::unique_lock<std::mutex> guard(_loader_cache_mutex);
    _map.erase(filename);
    _queue.remove(filename);
}
