/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__TILE_CACHE_H_
#define CARTA_BACKEND__TILE_CACHE_H_

#include <cstdint>
#include <functional>
#include <list>
#include <unordered_map>
#include <vector>

#include "Cache/TileCacheKey.h"
#include "Cache/TilePool.h"
#include "ImageData/FileLoader.h"

#define MAX_TILE_CACHE_CAPACITY 4096

namespace carta {

using TilePtr = std::shared_ptr<std::vector<float>>;

class TileCache {
public:
    using Key = TileCacheKey;

    virtual ~TileCache() = default;

    virtual TilePtr Peek(Key key) = 0;
    virtual TilePtr Get(Key key, std::shared_ptr<FileLoader> loader, std::mutex& image_mutex) = 0;
    virtual void Reset(int32_t z, int32_t stokes, int capacity = 0) = 0;

    static Key ChunkKey(Key tile_key);
    static TileCache* GetTileCache();
};

class PooledTileCache : public TileCache {
public:
    PooledTileCache();
    // This is read-only and does not lock the cache
    TilePtr Peek(Key key) override;
    // These functions lock the cache
    TilePtr Get(Key key, std::shared_ptr<FileLoader> loader, std::mutex& image_mutex) override;
    void Reset(int32_t z, int32_t stokes, int capacity = 0) override;

private:
    using TilePair = std::pair<Key, TilePtr>;
    using TileIter = std::vector<float>::iterator;

    TilePtr UnsafePeek(Key key);
    void Touch(Key key);
    bool LoadChunk(Key chunk_key, std::shared_ptr<FileLoader> loader, std::mutex& image_mutex);

    int32_t _z;
    int32_t _stokes;
    std::list<TilePair> _queue;
    std::unordered_map<Key, std::list<TilePair>::iterator> _map;
    int _capacity;
    std::mutex _tile_cache_mutex;

    std::vector<float> _chunk;
    std::shared_ptr<TilePool> _pool;
};

} // namespace carta

#endif // CARTA_BACKEND__TILE_CACHE_H_
