#include "TileCache.h";

CachedTilePtr TileCache::Peek(CachedTileKey key) {
    // This is a read-only operation which it is safe to do in parallel.
    if (_map.find(key) == _map.end()) {
        return CachedTilePtr();
    } else {
        return UnsafePeek(key);
    }
}
    
CachedTilePtr TileCache::Get(CachedTileKey key, const carta::FileLoader* loader) {
    // Will be loaded or retrieved from cache
    CachedTilePtr tile;
    
    if(_map.find(key) == _map.end()) { // Not in cache
        // Evict oldest tile if necessary
        if(_map.size() == _capacity) {
            _map.erase(_queue.back()->first);
            _queue.pop_back();
        }
        // Load new tile from image
        tile = Load(key, loader);
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

void TileCache::GetMultiple(&std::unordered_map<CachedTileKey, CachedTilePtr> tiles, std::vector<CachedTileKey> keys, const carta::FileLoader* loader) {
    // TODO: first process all tiles found in the cache in parallel, then all the tiles not in the cache serially.
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
            
    auto range = tbb::blocked_range<size_t>(0, found.size());
    
    auto loop = [&](const tbb::blocked_range<size_t>& r) {
        for (size_t t = r.begin(); t != r.end(); ++t) {
            auto& key = found[t];
            tiles[key] = UnsafePeek(key);
        }
    };
    
    for (auto& t : found) {
        Touch(*t);
    }
    
    tbb::parallel_for(range, loop);
    
    for (auto& t : not_found) {
        tiles[*t] = Get(*t);
    }
    
    lock.unlock();
}

void TileCache::reset(hsize_t channel, hsize_t stokes) {
    std::unique_lock<std::mutex> lock(_tile_cache_mutex);
    _map.clear();
    _queue.clear();
    _channel = channel;
    _stokes = stokes;
    lock.unlock();
}

CachedTilePtr TileCache::UnsafePeek(CachedTileKey key) {
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

CachedTilePtr TileCache::Load(CachedTileKey key, const carta::FileLoader* loader, std::mutex image_mutex) {
    // load a tile from the file
    
    auto tile = std::make_shared<std::vector<float>>(TILE_SIZE * TILE_SIZE);
    casacore::Array<float> tmp(slicer.length(), tile->data(), casacore::StorageInitPolicy::SHARE);
    
    casacore::Slicer slicer;
    if (_num_dims == 4) {
        slicer = casacore::Slicer(IPos(4, key.x, key.y, _channel, _stokes), IPos(4, TILE_SIZE, TILE_SIZE, 1, 1));
    } else if (_num_dims == 3) {
        slicer = casacore::Slicer(IPos(3, key.x, key.y, _channel), IPos(3, TILE_SIZE, TILE_SIZE, 1));
    } else if (_num_dims == 2) {
        slicer = casacore::Slicer(IPos(2, key.x, key.y), IPos(2, TILE_SIZE, TILE_SIZE));
    }
    
    std::unique_lock<std::mutex> image_lock(image_mutex);
    loader->GetSlice(tmp, slicer);
    image_lock.unlock()
    
    return tile;
}
