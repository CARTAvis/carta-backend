/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "TileCache.h"
#include <functional>

// TILE POOL

void TilePool::Grow(int size) {
    _capacity += size;
}

TilePtr TilePool::Pull() {
    std::unique_lock<std::mutex> guard(_tile_pool_mutex);

    if (_stack.empty()) {
        _stack.push(Create());
    }

    auto tile = _stack.top();
    _stack.pop();

    // Attach custom deleter to pool
    auto deleter = std::get_deleter<TilePool::TilePtrDeleter>(tile);
    deleter->_pool = std::move(this->weak_from_this());

    return tile;
}

void TilePool::Push(std::unique_ptr<std::vector<float>>& unique_tile) noexcept {
    TilePtr tile(unique_tile.release(), TilePool::TilePtrDeleter());
    std::unique_lock<std::mutex> guard(_tile_pool_mutex);
    _stack.push(tile);
}

bool TilePool::Full() {
    return _stack.size() >= _capacity;
}

TilePtr TilePool::Create() {
    auto unique_tile = std::make_unique<std::vector<float>>(TILE_SIZE * TILE_SIZE, NAN);
    TilePtr tile(unique_tile.release(), TilePool::TilePtrDeleter());
    return tile;
}

void TilePool::TilePtrDeleter::operator()(std::vector<float>* raw_tile) const noexcept {
    std::unique_ptr<std::vector<float>> unique_tile(raw_tile);
    if (const auto pool = _pool.lock()) {
        if (!pool->Full()) {
            pool->Push(unique_tile);
        }
    }
}

// TILE CACHE

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

TilePtr TileCache::Get(Key key, std::shared_ptr<carta::FileLoader> loader, std::mutex& image_mutex) {
    // Will be loaded or retrieved from cache
    bool valid(1);

    std::unique_lock<std::mutex> guard(_tile_cache_mutex);

    if (_map.find(key) == _map.end()) { // Not in cache
        // Load 2x2 chunk of tiles from image
        valid = LoadChunk(ChunkKey(key), loader, image_mutex);
    } else {
        Touch(key);
    }

    if (valid) {
        return UnsafePeek(key);
    }

    return nullptr;
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

bool TileCache::LoadChunk(Key chunk_key, std::shared_ptr<carta::FileLoader> loader, std::mutex& image_mutex) {
    // load a chunk from the file
    int data_width;
    int data_height;

    if (!loader->GetChunk(_chunk, data_width, data_height, chunk_key.x, chunk_key.y, _z, _stokes, image_mutex)) {
        return false;
    };

    // split the chunk into four tiles
    std::vector<TilePtr> tiles;
    std::vector<int> tile_widths;
    std::vector<int> tile_heights;

    for (int tile_row = 0; tile_row < 2; tile_row++) {
        for (int tile_col = 0; tile_col < 2; tile_col++) {
            tile_widths.push_back(std::max(0, std::min(TILE_SIZE, data_width - tile_col * TILE_SIZE)));
            tile_heights.push_back(std::max(0, std::min(TILE_SIZE, data_height - tile_row * TILE_SIZE)));
            tiles.push_back(_pool->Pull());
            tiles.back()->resize(tile_widths.back() * tile_heights.back());
        }
    }

    std::function<void(TileIter, int, TileIter&)> do_nothing = [&](TileIter start, int width, TileIter& destination) {};

    std::function<void(TileIter, int, TileIter&)> do_copy = [&](TileIter start, int width, TileIter& destination) {
        std::copy(start, start + width, destination);
        std::advance(destination, width);
    };

    auto left_copy = do_copy;
    auto right_copy = (data_width > TILE_SIZE) ? do_copy : do_nothing;

    auto pos = _chunk.begin();
    auto row_read_end = pos;

    for (int tr : {0, 1}) {
        auto row_height = tile_heights[tr * 2];

        if (!row_height) {
            continue;
        }

        auto left = tiles[tr * 2]->begin();
        auto right = tiles[tr * 2 + 1]->begin();

        auto left_width = tile_widths[tr * 2];
        auto right_width = tile_widths[tr * 2 + 1];

        row_read_end += data_width * row_height;

        while (pos < row_read_end) {
            left_copy(pos, left_width, left);
            right_copy(pos + TILE_SIZE, right_width, right);
            std::advance(pos, data_width);
        }
    }

    // insert the 4 tiles into the cache
    std::vector<std::pair<int, int>> offsets = {{0, 0}, {TILE_SIZE, 0}, {0, TILE_SIZE}, {TILE_SIZE, TILE_SIZE}};
    auto tile_offset = offsets.begin();
    for (auto& t : tiles) {
        Key key(chunk_key.x + tile_offset->first, chunk_key.y + tile_offset->second);

        if (key.x < chunk_key.x + data_width && key.y < chunk_key.y + data_height) {
            // this tile is within the bounds of the image

            // If the tile is not in the map
            if (_map.find(key) == _map.end()) { // add if not found
                // Evict oldest tile if necessary
                if (_map.size() == _capacity) {
                    _map.erase(_queue.back().first);
                    _queue.pop_back();
                }

                // Insert the new tile
                _queue.push_front(std::make_pair(key, t));
                _map[key] = _queue.begin();

            } else { // touch the tile
                Touch(key);
            }
        }

        std::advance(tile_offset, 1);
    }

    return true;
}
