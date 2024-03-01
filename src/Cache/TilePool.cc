/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2024 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Cache/TilePool.h"
#include "Util/Image.h"

using namespace carta;

void TilePool::Grow(int size) {
    _capacity += size;
}

TilePtr TilePool::Pull() {
    std::unique_lock<std::mutex> guard(_tile_pool_mutex);

    if (_stack.empty()) {
        _stack.push(Create());
    }

    auto tile = _stack.top();
    _stack.pop();

    // Attach custom deleter to pool
    auto deleter = std::get_deleter<TilePool::TilePtrDeleter>(tile);
    deleter->_pool = std::move(this->weak_from_this());

    return tile;
}

[[maybe_unused]] void TilePool::Push(std::unique_ptr<std::vector<float>>& unique_tile) noexcept {
    TilePtr tile(unique_tile.release(), TilePool::TilePtrDeleter());
    std::unique_lock<std::mutex> guard(_tile_pool_mutex);
    _stack.push(tile);
}

bool TilePool::Full() {
    return _stack.size() >= _capacity;
}

TilePtr TilePool::Create() {
    auto unique_tile = std::make_unique<std::vector<float>>(TILE_SIZE * TILE_SIZE, NAN);
    TilePtr tile(unique_tile.release(), TilePool::TilePtrDeleter());
    return tile;
}

void TilePool::TilePtrDeleter::operator()(std::vector<float>* raw_tile) const noexcept {
    std::unique_ptr<std::vector<float>> unique_tile(raw_tile);
    if (const auto pool = _pool.lock()) {
        if (!pool->Full()) {
            pool->Push(unique_tile);
        }
    }
}
