#ifndef CARTA_BACKEND__TILE_CACHE_H_
#define CARTA_BACKEND__TILE_CACHE_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include <unordered_map>
#include <list>

#include "ImageData/FileLoader.h"

struct Key {
    Key() {}
    Key(int32_t x, int32_t y) : x(x), y(y) {}
    
    bool operator==(const Key &other) const { 
        return (x == other.x && y == other.y);
    }
    
    int32_t x;
    int32_t y;
};

namespace std {
  template <>
  struct hash<Key>
  {
    std::size_t operator()(const Key& k) const
    {
        return std::hash<int32_t>()(k.x) ^ (std::hash<int32_t>()(k.x) << 1);
    }
  };
}

class TileCache {
using TilePtr = std::shared_ptr<std::vector<float>>;
using TilePair = std::pair<Key, TilePtr>;
public:
    TileCache() {}
    TileCache(int capacity) : _capacity(capacity), _channel(0), _stokes(0) {}
    
    // This is read-only and does not lock the cache
    bool Peek(std::vector<float>& tile_data, Key key);
    
    // These functions lock the cache
    bool Get(std::vector<float>& tile_data, Key key, carta::FileLoader* loader, std::mutex& image_mutex);
    bool GetMultiple(std::unordered_map<Key, std::vector<float>>& tiles, carta::FileLoader* loader, std::mutex& image_mutex);
    void Reset(int32_t channel, int32_t stokes);
    
private:
    void CopyTileData(std::vector<float>& tile_data, TilePtr& tile);
    TilePtr UnsafePeek(Key key);
    void Touch(Key key);
    Key ChunkKey(Key tile_key);
    bool LoadChunk(Key chunk_key, carta::FileLoader* loader, std::mutex& image_mutex);
    
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
