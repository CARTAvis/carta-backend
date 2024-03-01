/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2024 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_CACHE_TILECACHEKEY_H_
#define CARTA_SRC_CACHE_TILECACHEKEY_H_

namespace carta {
/** @brief Key for tiles used in TileCache
 *  @details This is used in TileCache to identify tiles.
 *  @see std::hash<carta::TileCacheKey>
 */
struct TileCacheKey {
    /** @brief Default constructor */
    TileCacheKey() {}
    /** @brief Constructor from tile coordinates
     *  @param x The X coordinate of the tile
     *  @param y The Y coordinate of the tile
     */
    TileCacheKey(int32_t x, int32_t y) : x(x), y(y) {}
    /** @brief Equality operator
     *  @param other Another key
     *  @return Whether the keys are equal
     *  @details Keys are equal if their coordinates are equal.
     */
    bool operator==(const TileCacheKey& other) const {
        return (x == other.x && y == other.y);
    }
    /** @brief The X coordinate */
    int32_t x;
    /** @brief The Y coordinate */
    int32_t y;
};

} // namespace carta

namespace std {
/** @brief Hash for carta::TileCacheKey
 *  @details This allows carta::TileCacheKey to be used as a key in the unordered map in carta::TileCache.
 *  @see carta::TileCacheKey
 */
template <>
struct hash<carta::TileCacheKey> {
    /** @brief The operator which calculates the hash
     *  @param k The key
     *  @return The key hash
     *  @details The hash is calculated from the hashes of the coordinates of the key.
     */
    std::size_t operator()(const carta::TileCacheKey& k) const {
        return std::hash<int32_t>()(k.x) ^ (std::hash<int32_t>()(k.y) << 1);
    }
};
} // namespace std

#endif // CARTA_SRC_CACHE_TILECACHEKEY_H_
