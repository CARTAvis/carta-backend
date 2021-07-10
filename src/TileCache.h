/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__TILE_CACHE_H_
#define CARTA_BACKEND__TILE_CACHE_H_

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <stack>
#include <unordered_map>
#include <vector>

#include "ImageData/FileLoader.h"

using TilePtr = std::shared_ptr<std::vector<float>>;

struct TilePool : std::enable_shared_from_this<TilePool> {
    TilePool() : _capacity(4) {}
    void Grow(int size);
    TilePtr Pull();
    void Push(std::unique_ptr<std::vector<float>>& unique_tile) noexcept;
    bool Full();

private:
    TilePtr Create();

    std::mutex _tile_pool_mutex;
    std::stack<TilePtr> _stack;
    // The capacity of the pool should be 4 more than the capacity of the cache, so that we can always load a chunk before evicting
    // anything.
    int _capacity;

    struct TilePtrDeleter {
        std::weak_ptr<TilePool> _pool;
        TilePtrDeleter() noexcept = default;
        explicit TilePtrDeleter(std::weak_ptr<TilePool>&& pool) noexcept : _pool(std::move(pool)) {}
        void operator()(std::vector<float>* raw_tile) const noexcept;
    };
};

struct TileCacheKey {
    TileCacheKey() {}
    TileCacheKey(int32_t x, int32_t y) : x(x), y(y) {}

    bool operator==(const TileCacheKey& other) const {
        return (x == other.x && y == other.y);
    }

    int32_t x;
    int32_t y;
};

namespace std {
template <>
struct hash<TileCacheKey> {
    std::size_t operator()(const TileCacheKey& k) const {
        return std::hash<int32_t>()(k.x) ^ (std::hash<int32_t>()(k.x) << 1);
    }
};
} // namespace std

class TileCache {
public:
    using Key = TileCacheKey;

    TileCache() {}
    TileCache(int capacity);

    // This is read-only and does not lock the cache
    TilePtr Peek(Key key);

    // These functions lock the cache
    TilePtr Get(Key key, std::shared_ptr<carta::FileLoader> loader, std::mutex& image_mutex);
    void Reset(int32_t z, int32_t stokes, int capacity = 0);

    static Key ChunkKey(Key tile_key);

private:
    using TilePair = std::pair<Key, TilePtr>;
    using TileIter = std::vector<float>::iterator;

    TilePtr UnsafePeek(Key key);
    void Touch(Key key);
    bool LoadChunk(Key chunk_key, std::shared_ptr<carta::FileLoader> loader, std::mutex& image_mutex);

    int32_t _z;
    int32_t _stokes;
    std::list<TilePair> _queue;
    std::unordered_map<Key, std::list<TilePair>::iterator> _map;
    int _capacity;
    std::mutex _tile_cache_mutex;

    std::vector<float> _chunk;
    std::shared_ptr<TilePool> _pool;
};

#endif // CARTA_BACKEND__TILE_CACHE_H_
