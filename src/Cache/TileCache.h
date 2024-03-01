/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2024 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_CACHE_TILECACHE_H_
#define CARTA_SRC_CACHE_TILECACHE_H_

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

/** @brief A cache for full-resolution image tiles.
 *  @details A tile cache is used by Frame instead of a full image cache if its FileLoader reports that it should be used. Currently only
 * the Hdf5Loader implements this. This implementation uses a pool to store reusable tile objects. This is an LRU cache: when tile capacity
 * is reached, the least recently used tile is discarded first.
 *  @see FileLoader::UseTileCache
 *  @see TilePool
 *  @see TileCacheKey
 */
class TileCache {
public:
    using Key = TileCacheKey;

    /** @brief Default constructor */
    TileCache() {}
    /** @brief Constructor used by Frame
     *  @param capacity The cache capacity
     */
    TileCache(int capacity);

    /** @brief Retrieve a tile from the cache without modifying its access time
     *  @param key The tile key
     *  @details This is a read-only operation and does not lock the cache.
     */
    TilePtr Peek(Key key);

    /** @brief Retrieve a tile from the cache
     *  @param key The tile key
     *  @details This function locks the cache because it modifies the cache state.
     */
    TilePtr Get(Key key, std::shared_ptr<FileLoader> loader, std::mutex& image_mutex);
    /** @brief Reset the cache for a new Z coordinate and/or Stokes coordinate, clearing all tiles.
     *  @param z The new Z coordinate
     *  @param stokes The new Stokes coordinate
     *  @param capacity The new capacity
     *  @details This function locks the cache because it modifies the cache state.
     */
    void Reset(int32_t z, int32_t stokes, int capacity = 0);

    /** @brief Calculate the key for the chunk that contains the given tile
     *  @param tile_key The tile key
     *  @return The chunk key
     *  @details HDF5 files produced by the [fits2idia converter](https://github.com/CARTAvis/fits2idia) currently use a chunk size which is
     * twice the tile size in each dimension. This means that each chunk is a block of 2x2 tiles.
     *  @see LoadChunk
     */
    static Key ChunkKey(Key tile_key);

private:
    using TilePair = std::pair<Key, TilePtr>;
    using TileIter = std::vector<float>::iterator;

    /** @brief Retrieve a tile from the cache.
     *  @param key The tile key
     *  @details This function does not lock the cache, and assumes that the tile is in the cache.
     */
    TilePtr UnsafePeek(Key key);
    /** @brief Update the access time of a tile.
     *  @param key The tile key
     *  @details This function does not lock the cache, and assumes that the tile is in the cache.
     */
    void Touch(Key key);
    /** @brief Load a chunk from the file into the cache.
     *  @param chunk_key The key of the chunk.
     *  @param loader The file loader.
     *  @param image_mutex The image mutex to pass to FileLoader::GetChunk.
     *  @details When a requested tile is not found in the cache, for efficiency we read the entire chunk of data which contains that tile,
     * and add all the tiles contained in the chunk into the cache at once.
     *  @see ChunkKey
     */
    bool LoadChunk(Key chunk_key, std::shared_ptr<FileLoader> loader, std::mutex& image_mutex);

    /** @brief The current Z coordinate. */
    int32_t _z;
    /** @brief The current Stokes coordinate. */
    int32_t _stokes;
    /** @brief The queue which stores tiles in order of access time. */
    std::list<TilePair> _queue;
    /** @brief The map which stores references to tiles in the queue. */
    std::unordered_map<Key, std::list<TilePair>::iterator> _map;
    /** @brief The maximum number of tiles which may be stored in the cache. */
    int _capacity;
    /** @brief The cache mutex. */
    std::mutex _tile_cache_mutex;

    /** @brief The vector used to load chunk data. */
    std::vector<float> _chunk;
    /** @brief The pool used to store reusable tile objects. */
    std::shared_ptr<TilePool> _pool;
};

} // namespace carta

#endif // CARTA_SRC_CACHE_TILECACHE_H_
