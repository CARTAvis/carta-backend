#include "TileCache.h"

TileCache::CachedTilePtr TileCache::Peek(CachedTileKey key) {
    // This is a read-only operation which it is safe to do in parallel.
    if (_map.find(key) == _map.end()) {
        return TileCache::CachedTilePtr();
    } else {
        return UnsafePeek(key);
    }
}
    
TileCache::CachedTilePtr TileCache::Get(CachedTileKey key, carta::FileLoader* loader, std::mutex& image_mutex) {
    // Will be loaded or retrieved from cache
    TileCache::CachedTilePtr tile;
    
    if(_map.find(key) == _map.end()) { // Not in cache
        // Evict oldest tile if necessary
        if(_map.size() == _capacity) {
            _map.erase(_queue.back().first);
            _queue.pop_back();
        }
        // Load new tile from image
        tile = Load(key, loader, image_mutex);
    } else {
        // Remove found tile from queue, to reinsert
        tile = _map.find(key)->second->second;
        _queue.erase(_map.find(key)->second); 
    }
    
    // Insert new or retrieved tile into the front of the queue
    _queue.push_front(std::make_pair(key, tile));
    // Insert or update the hash entry
    _map[key] = _queue.begin();
    
    return tile;
}

void TileCache::GetMultiple(std::unordered_map<CachedTileKey, TileCache::CachedTilePtr>& tiles, std::vector<CachedTileKey> keys, carta::FileLoader* loader, std::mutex& image_mutex) {
    std::vector<CachedTileKey> found;
    std::vector<CachedTileKey> not_found;
    
    std::unique_lock<std::mutex> lock(_tile_cache_mutex);
    
    for (auto& key : keys) {
        if (_map.find(key) == _map.end()) {
            not_found.push_back(key);
        } else {
            found.push_back(key);
        }
    }
    
#pragma omp parallel for
    for (auto& key : found) {
        tiles[key] = UnsafePeek(key);
    }

    for (auto& key : found) {
        Touch(key);
    }
        
    for (auto& key : not_found) {
        tiles[key] = Get(key, loader, image_mutex);
    }
    
    lock.unlock();
}

void TileCache::reset(int32_t channel, int32_t stokes) {
    std::unique_lock<std::mutex> lock(_tile_cache_mutex);
    _map.clear();
    _queue.clear();
    _channel = channel;
    _stokes = stokes;
    lock.unlock();
}

TileCache::CachedTilePtr TileCache::UnsafePeek(CachedTileKey key) {
    // Assumes that the tile is in the cache
    return _map.find(key)->second->second;
}

void TileCache::Touch(CachedTileKey key) {
    // Move tile to the front of the queue
    // Assumes that the tile is in the cache
    auto tile = _map.find(key)->second->second;
    _queue.erase(_map.find(key)->second);
    _queue.push_front(std::make_pair(key, tile));
    _map[key] = _queue.begin();
}

TileCache::CachedTilePtr TileCache::Load(CachedTileKey key, carta::FileLoader* loader, std::mutex& image_mutex) {
    // load a tile from the file
    // TODO: how to handle errors?
    auto tile = std::make_shared<std::vector<float>>(TILE_SIZE * TILE_SIZE);
    loader->GetTile(*tile, key.x, key.y, _channel, _stokes, image_mutex);
    return tile;
}
