#ifndef CARTA_BACKEND__TILE_CACHE_H_
#define CARTA_BACKEND__TILE_CACHE_H_

// TODO: implement operators needed for use as key.
struct CachedTileKey {
    CachedTileKey() {}
    CachedTileKey(hsize_t x, hsize_t y) : x(x), y(y) {}
    hsize_t x;
    hsize_t y;
};

class TileCache {
using CachedTilePtr = std::shared_ptr<std::vector<float>>;
using CachedTilePair = std::pair<CachedTileKey, CachedTilePtr>;
public:
    TileCache() {}
    TileCache(int capacity) : _capacity(capacity) {}
    
    CachedTilePtr Peek(CachedTileKey key);
    CachedTilePtr Get(CachedTileKey key, const carta::FileLoader* loader);
    void GetMultiple(&std::unordered_map<CachedTileKey, CachedTilePtr> tiles, std::vector<CachedTileKey> keys, const carta::FileLoader* loader);
    
    void Lock();
    void Unlock();
    
    void reset(hsize_t channel, hsize_t stokes);
    
private:
    CachedTilePtr UnsafePeek(CachedTileKey key);
    void Touch(CachedTileKey key);
    CachedTilePtr Load(CachedTileKey key, const carta::FileLoader* loader);
    
    hsize_t _channel;
    hsize_t _stokes;
    std::list<CachedTilePair> _queue;
    std::unordered_map<CachedTileKey, std::list<CachedTilePair>::iterator> _hash;
    int _capacity;
    std::mutex _tile_cache_mutex; // maybe change to tbb mutex
};

#endif // CARTA_BACKEND__TILE_CACHE_H_
