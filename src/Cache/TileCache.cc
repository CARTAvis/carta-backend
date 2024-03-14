/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "TileCache.h"

#include "Util/Image.h"

using namespace carta;

TileCache::TileCache(int capacity) : _capacity(capacity), _z(0), _stokes(0), _pool(std::make_shared<TilePool>()) {
    std::unique_lock<std::mutex> guard(_tile_cache_mutex);
    _pool->Grow(capacity);
}

TilePtr TileCache::Peek(Key key) {
    // This is a read-only operation which it is safe to do in parallel.
    if (_map.find(key) == _map.end()) {
        return nullptr;
    } else {
        return UnsafePeek(key);
    }
}

void TileCache::Reset(int32_t z, int32_t stokes, int capacity) {
    std::unique_lock<std::mutex> guard(_tile_cache_mutex);
    if (capacity > 0) {
        _pool->Grow(capacity - _capacity);
        _capacity = capacity;
    }
    _map.clear();
    _queue.clear();
    _z = z;
    _stokes = stokes;
}

TilePtr TileCache::UnsafePeek(Key key) {
    // Assumes that the tile is in the cache
    return _map.find(key)->second->second;
}

void TileCache::Touch(Key key) {
    // Move tile to the front of the queue
    // Assumes that the tile is in the cache
    auto tile = _map.find(key)->second->second;
    _queue.erase(_map.find(key)->second);
    _queue.push_front(std::make_pair(key, tile));
    _map[key] = _queue.begin();
}

TileCache::Key TileCache::ChunkKey(Key tile_key) {
    return Key((tile_key.x / CHUNK_SIZE) * CHUNK_SIZE, (tile_key.y / CHUNK_SIZE) * CHUNK_SIZE);
}
