#include "TileCache.h"

bool TileCache::Peek(std::vector<float>& tile_data, Key key) {
    // This is a read-only operation which it is safe to do in parallel.
    if (_map.find(key) == _map.end()) {
        return false;
    } else {
        return UnsafePeek(tile_data, key);
    }
}
    
bool TileCache::Get(std::vector<float>& tile_data, Key key, carta::FileLoader* loader, std::mutex& image_mutex) {
    // Will be loaded or retrieved from cache
    TileCache::TilePtr tile;
    bool valid(1);
    
    if(_map.find(key) == _map.end()) { // Not in cache
        // Evict oldest tile if necessary
        if(_map.size() == _capacity) {
            _map.erase(_queue.back().first);
            _queue.pop_back();
        }
        // Load new tile from image
        tile = std::make_shared<std::vector<float>>(TILE_SIZE * TILE_SIZE);
        valid = Load(tile, key, loader, image_mutex);
    } else {
        // Remove found tile from queue, to reinsert
        tile = _map.find(key)->second->second;
        _queue.erase(_map.find(key)->second);
    }
    
    // Insert new or retrieved tile into the front of the queue
    _queue.push_front(std::make_pair(key, tile));
    // Insert or update the hash entry
    _map[key] = _queue.begin();
    
    if (valid) {
        CopyTileData(tile_data, tile);
    }
    
    return valid;
}

bool TileCache::GetMultiple(std::unordered_map<Key, std::vector<float>>& tiles, carta::FileLoader* loader, std::mutex& image_mutex) {
    std::vector<Key> found;
    std::vector<Key> not_found;
    bool valid(1);
    
    std::unique_lock<std::mutex> lock(_tile_cache_mutex);
    
    for (auto& kv : tiles) {
        Key& key = kv->first;
        if (_map.find(key) == _map.end()) {
            not_found.push_back(key);
        } else {
            found.push_back(key);
        }
    }
    
#pragma omp parallel for
    for (auto& key : found) {
        CopyData(tiles[key], UnsafePeek(key));
    }

    for (auto& key : found) {
        Touch(key);
    }
        
    for (auto& key : not_found) {
        valid = valid && Get(tiles[key], key, loader, image_mutex);
    }
    
    lock.unlock();
    
    return valid;
}

void TileCache::Reset(int32_t channel, int32_t stokes, int capacity) {
    std::unique_lock<std::mutex> lock(_tile_cache_mutex);
    if (capacity > 0) {
        _capacity = capacity;
    }
    _map.clear();
    _queue.clear();
    _channel = channel;
    _stokes = stokes;
    lock.unlock();
}

std::mutex TileCache::GetMutex() {
    return _tile_cache_mutex;
}

TileCache::CopyTileData(std::vector<float>& tile_data, TilePtr& tile) {
    tile_data.resize(tile->size());
    std::copy(tile->begin(), tile->end(), tile_data.begin());
}

TileCache::TilePtr TileCache::UnsafePeek(Key key) {
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

bool TileCache::Load(TileCache::TilePtr& tile, Key key, carta::FileLoader* loader, std::mutex& image_mutex) {
    // load a tile from the file
    return loader->GetTile(*tile, key.x, key.y, _channel, _stokes, image_mutex);
}
