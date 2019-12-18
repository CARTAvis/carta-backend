#include "TileCache.h";

CachedTilePtr TileCache::Peek(CachedTileKey key) {
    // This is a read-only operation which it is safe to do in parallel.
    if (_hash.find(key) == _hash.end()) {
        return CachedTilePtr();
    } else {
        return UnsafePeek(key);
    }
}
    
CachedTilePtr TileCache::Get(CachedTileKey key, const carta::FileLoader* loader) {
    // Will be loaded or retrieved from cache
    CachedTilePtr tile;
    
    if(_hash.find(key) == _hash.end()) { // Not in cache
        // Evict oldest tile if necessary
        if(_hash.size() == _capacity) {
            _hash.erase(_queue.back()->first);
            _queue.pop_back();
        }
        // Load new tile from image
        tile = Load(key, loader);
    } else {
        // Remove found tile from queue, to reinsert
        tile = _hash.find(key)->second->second;
        _queue.erase(_hash.find(key)->second); 
    }
    
    // Insert new or retrieved tile into the front of the queue
    _queue.push_front(std::make_pair(key, tile));
    // Insert or update the hash entry
    _hash[key] = _queue.begin();
    
    return tile;
}

void TileCache::GetMultiple(&std::unordered_map<CachedTileKey, CachedTilePtr> tiles, std::vector<CachedTileKey> keys, const carta::FileLoader* loader) {
    // TODO: first process all tiles found in the cache in parallel, then all the tiles not in the cache serially.
    // Make thread-safe.
    std::vector<CachedTileKey> found;
    std::vector<CachedTileKey> not_found;
    
    Lock();
    
    for (auto& key : keys) {
        if (_hash.find(key) == _hash.end()) {
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
    
    Unlock();
}

void TileCache::Lock() {
    // TODO: lock the cache
}

void TileCache::Unlock() {
    // TODO: unlock the cache
}

void TileCache::reset(hsize_t channel, hsize_t stokes) {
    Lock();
    _hash.clear();
    _queue.clear();
    _channel = channel;
    _stokes = stokes;
    Unlock();
}

CachedTilePtr TileCache::UnsafePeek(CachedTileKey key) {
    // Assumes that the tile is in the cache
    return _hash.find(key)->second->second;
}

void TileCache::Touch(CachedTileKey key) {
    // Move tile to the front of the queue
    // Assumes that the tile is in the cache
    auto tile = _hash.find(key)->second->second;
    _queue.erase(_hash.find(key)->second);
    _queue.push_front(std::make_pair(key, tile));
    _hash[key] = _queue.begin();
}

CachedTilePtr TileCache::Load(CachedTileKey key, const carta::FileLoader* loader) {
    // load a tile from the file
    // loader->GetSlice(????);
}
