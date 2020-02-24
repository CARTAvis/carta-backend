#ifndef CARTA_BACKEND__TILE_CACHE_H_
#define CARTA_BACKEND__TILE_CACHE_H_

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <unordered_map>
#include <vector>

#include "ImageData/FileLoader.h"

struct TileCacheKey {
    TileCacheKey() {}
    TileCacheKey(int32_t x, int32_t y) : x(x), y(y) {}

    bool operator==(const TileCacheKey& other) const {
        return (x == other.x && y == other.y);
    }

    int32_t x;
    int32_t y;

    friend std::ostream& operator<<(std::ostream& os, const TileCacheKey& key);
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
    using TilePtr = std::shared_ptr<std::vector<float>>;

    TileCache() {}
    TileCache(int capacity) : _capacity(capacity), _channel(0), _stokes(0) {}

    // This is read-only and does not lock the cache
    TilePtr Peek(Key key);

    // These functions lock the cache
    TilePtr Get(Key key, std::shared_ptr<carta::FileLoader> loader, std::mutex& image_mutex);
    std::unordered_map<Key, TilePtr> GetMultiple(
        std::vector<Key>& keys, std::shared_ptr<carta::FileLoader> loader, std::mutex& image_mutex);
    void Reset(int32_t channel, int32_t stokes, int capacity = 0);

private:
    using TilePair = std::pair<Key, TilePtr>;
    using TileIter = std::vector<float>::iterator;

    TilePtr UnsafePeek(Key key);
    void Touch(Key key);
    Key ChunkKey(Key tile_key);
    bool LoadChunk(Key chunk_key, std::shared_ptr<carta::FileLoader> loader, std::mutex& image_mutex);

    int32_t _channel;
    int32_t _stokes;
    std::list<TilePair> _queue;
    std::unordered_map<Key, std::list<TilePair>::iterator> _map;
    int _capacity;
    std::mutex _tile_cache_mutex;

    static const int _TILE_SQ;
    static const int _CHUNK_SIZE;
    static const int _CHUNK_SQ;
};

#endif // CARTA_BACKEND__TILE_CACHE_H_
