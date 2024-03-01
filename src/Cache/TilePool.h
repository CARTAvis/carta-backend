/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2024 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_CACHE_TILEPOOL_H_
#define CARTA_SRC_CACHE_TILEPOOL_H_

#include <memory>
#include <mutex>
#include <stack>
#include <vector>

namespace carta {

using TilePtr = std::shared_ptr<std::vector<float>>;

/** @brief This struct stores the memory allocated for cached tile data.
 *  @details Instead of repeatedly allocating and freeing memory for cached tile data, which has a significant performance cost, we keep a
 * pool of allocated tile objects which are reused as tiles are read and discarded (up to a given capacity). The capacity of the pool should
 * be 4 more than the capacity of the cache, so that we can always load a chunk before evicting anything.
 */
struct TilePool : std::enable_shared_from_this<TilePool> {
    /** @brief Constructor */
    TilePool() : _capacity(4) {}
    /** @brief Grow the capacity of the pool.
     *  @param size the size increment to be added
     */
    void Grow(int size);
    /** @brief Request a tile object from the pool.
     *  @return A tile object.
     *  @details If the pool is empty, a new tile object will be created.
     */
    TilePtr Pull();
    /** @brief Return a tile object to the pool.
     *  @param unique_tile a unique pointer to a tile object
     *  @details This function is called from the custom deleter which is attached to tile objects obtained from this pool.
     */
    [[maybe_unused]] void Push(std::unique_ptr<std::vector<float>>& unique_tile) noexcept;
    /** @brief Check if the pool is full.
     *  @return Whether the pool has reached capacity.
     *  @details This function is called from the custom deleter which is attached to tile objects obtained from this pool.
     */
    bool Full();

private:
    /** @brief Allocate a new tile object.
     *  @return A tile object.
     */
    TilePtr Create();

    /** @brief The mutex used by functions which modify the stack. */
    std::mutex _tile_pool_mutex;
    /** @brief The stack where reusable tile objects are stored. */
    std::stack<TilePtr> _stack;
    /** @brief The maximum number of items which may be stored in the stack.
     *  @details When capacity is reached, discarded tile objects are really deleted instead of being returned to the pool.
     */
    int _capacity;

    /** @brief The custom deleter which allows discarded tiles to be returned to the pool. */
    struct TilePtrDeleter {
        /** @brief A reference to the pool.
         * @details If the pool has been deleted, discarded tiles will also be deleted.
         */
        std::weak_ptr<TilePool> _pool;
        /** @brief Default constructor */
        TilePtrDeleter() noexcept = default;
        /** @brief Constructor used by the pool
         * @param pool a reference to the pool
         */
        explicit TilePtrDeleter(std::weak_ptr<TilePool>&& pool) noexcept : _pool(std::move(pool)) {}
        /** @brief Operator which performs the deletion
         *  @details If the pool has reached capacity or been deleted, the object is deleted. Otherwise it is returned to the pool.
         */
        void operator()(std::vector<float>* raw_tile) const noexcept;
    };
};

} // namespace carta

#endif // CARTA_SRC_CACHE_TILEPOOL_H_
