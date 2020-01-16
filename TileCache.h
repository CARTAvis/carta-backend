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
    
    bool Peek(std::vector<float>& tile_data, Key key);
    bool Get(std::vector<float>& tile_data, Key key, carta::FileLoader* loader, std::mutex& image_mutex);
    bool GetMultiple(std::unordered_map<Key, std::vector<float>>& tiles, carta::FileLoader* loader, std::mutex& image_mutex);
    
    // Used to clear the cache when the channel changes, and to adjust the capacity when an image is loaded
    void Reset(int32_t channel, int32_t stokes, int capacity = 0);
    std::mutex GetMutex();
    
private:
    void CopyTileData(std::vector<float>& tile_data, TilePtr& tile);
    TilePtr UnsafePeek(Key key);
    void Touch(Key key);
    bool Load(TilePtr& tile, Key key, carta::FileLoader* loader, std::mutex& image_mutex);
    
    int32_t _channel;
    int32_t _stokes;
    std::list<TilePair> _queue;
    std::unordered_map<Key, std::list<TilePair>::iterator> _map;
    int _capacity;
    std::mutex _tile_cache_mutex;
};

#endif // CARTA_BACKEND__TILE_CACHE_H_
