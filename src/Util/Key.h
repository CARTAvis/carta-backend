/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_UTIL_KEY_H_
#define CARTA_SRC_UTIL_KEY_H_

#include <tuple>

namespace carta {

/**
 * @brief Class for grouping helper functions for shared key functionality.
 * @details Child struct constructors must pass their specific field values into the parent constructor to define the tuple representation
 * of the struct that will be used to check for equality and to construct a combined hash. The tuple elements don't have to be ordered in
 * the same way as the struct fields, and don't have to include all of the child properties (if some are not relevant). The tuple elements
 * should be ordered by the size of their likely range, from smallest to largest: this reduces the likelihood of hash collisions. For
 * example, a polarization index should be sorted before a Z index, but X and Y coordinates can be passed in either order.
 */
template <typename... T>
struct Key {
    Key(){};
    Key(std::tuple<T...> as_tuple) : as_tuple(as_tuple){};
    virtual ~Key() = default;

    /** @brief Generic function for creating a hash for a key object.
     *  @details This helper function implements hashes for custom key objects using their tuple representations.
     *  At time of writing there is no clean way for an object to inherit a `std::hash` specialization, so specializations must be defined
     * individually for children, and should call this function. This algorithm multiplies component hashes by increasing powers of a prime
     * number and sums them. For example, for a tuple with values `a1, a2, a3` that hash to `h1, h2, h3`, it returns `h1 + 31 * h2 + pow(31,
     * 2) * h3`. This produces a better hash distribution than XORing bit-shifted component hashes, even for quite large integer values of
     * components (relevant for X / Y / Z coordinates). 31 was selected as the prime multiplier because it can be implemented efficiently as
     * a shift and a subtraction.
     */
    std::size_t CombinedHash() const {
        std::size_t h(0);
        std::size_t m(1);

        std::apply(
            [&h, &m](const T&... arg) {
                std::size_t i{0};
                ((h += std::hash<T>{}(arg)*m, m *= 31), ...);
            },
            as_tuple);

        return h;
    }

    /** @brief Equality operator.
     *  @details This is a generic function that compares tuple representations.
     */
    bool operator==(const Key<T...>& other) const {
        return as_tuple == other.as_tuple;
    }

    /** @brief Inequality operator.
     *  @details This is a generic function that compares tuple representations.
     */
    bool operator!=(const Key<T...>& other) const {
        return as_tuple != other.as_tuple;
    }

    /** @brief Tuple representation */
    std::tuple<T...> as_tuple;
};

/** @brief A key containing a pair of (X, Y) coordinates. */
struct CoordinatesKey : public Key<int, int> {
    CoordinatesKey(){};
    CoordinatesKey(int x, int y) : Key({x, y}), x(x), y(y){};
    int x;
    int y;
};

/** @brief A key containing a polarization index and a Z index. */
struct ChannelKey : public Key<int, int> {
    ChannelKey(){};
    ChannelKey(int stokes_index, int z_index) : Key({stokes_index, z_index}), stokes_index(stokes_index), z_index(z_index){};
    int stokes_index;
    int z_index;
};

/** @brief A key containing a file ID and a region ID. */
struct FileRegionKey : public Key<int, int> {
    FileRegionKey(){};
    FileRegionKey(int file_id, int region_id) : Key({file_id, region_id}), file_id(file_id), region_id(region_id){};
    int file_id;
    int region_id;
};

/** @brief A key containing a file ID, a region ID, a polarization index, and a Z index. */
struct FileRegionChannelKey : public Key<int, int, int, int> {
    FileRegionChannelKey(){};
    FileRegionChannelKey(int file_id, int region_id, int stokes_index, int z_index = -1)
        : Key({file_id, region_id, stokes_index, z_index}),
          file_id(file_id),
          region_id(region_id),
          stokes_index(stokes_index),
          z_index(z_index){};
    int file_id;
    int region_id;
    int stokes_index;
    int z_index;
};

} // namespace carta

namespace std {

/** @brief Hash for carta::CoordinatesKey */
template <>
struct hash<carta::CoordinatesKey> {
    std::size_t operator()(const carta::CoordinatesKey& k) const {
        return k.CombinedHash();
    }
};

/** @brief Hash for carta::ChannelKey */
template <>
struct hash<carta::ChannelKey> {
    std::size_t operator()(const carta::ChannelKey& k) const {
        return k.CombinedHash();
    }
};

/** @brief Hash for carta::FileRegionKey */
template <>
struct hash<carta::FileRegionKey> {
    std::size_t operator()(const carta::FileRegionKey& k) const {
        return k.CombinedHash();
    }
};

/** @brief Hash for carta::FileRegionChannelKey */
template <>
struct hash<carta::FileRegionChannelKey> {
    std::size_t operator()(const carta::FileRegionChannelKey& k) const {
        return k.CombinedHash();
    }
};

} // namespace std

#endif // CARTA_SRC_UTIL_KEY_H_
