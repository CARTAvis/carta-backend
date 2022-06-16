/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_TILEPOOL_H
#define CARTA_BACKEND_TILEPOOL_H

#include <memory>
#include <mutex>
#include <stack>
#include <vector>

namespace carta {

using TilePtr = std::shared_ptr<std::vector<float>>;

struct TilePool : std::enable_shared_from_this<TilePool> {
    TilePool() : _capacity(4) {}
    void Grow(int size);
    TilePtr Pull();
    [[maybe_unused]] void Push(std::unique_ptr<std::vector<float>>& unique_tile) noexcept;
    bool Full();

private:
    TilePtr Create();

    std::mutex _tile_pool_mutex;
    std::stack<TilePtr> _stack;
    // The capacity of the pool should be 4 more than the capacity of the cache, so that we can always load a chunk before evicting
    // anything.
    int _capacity;

    struct TilePtrDeleter {
        std::weak_ptr<TilePool> _pool;
        TilePtrDeleter() noexcept = default;
        explicit TilePtrDeleter(std::weak_ptr<TilePool>&& pool) noexcept : _pool(std::move(pool)) {}
        void operator()(std::vector<float>* raw_tile) const noexcept;
    };
};

} // namespace carta

#endif // CARTA_BACKEND_TILEPOOL_H
