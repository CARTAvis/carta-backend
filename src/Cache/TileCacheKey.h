/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_TILECACHEKEY_H
#define CARTA_BACKEND_TILECACHEKEY_H

namespace carta {
struct TileCacheKey {
    TileCacheKey() {}
    TileCacheKey(int32_t x, int32_t y) : x(x), y(y) {}
    bool operator==(const TileCacheKey& other) const {
        return (x == other.x && y == other.y);
    }
    int32_t x;
    int32_t y;
};

} // namespace carta

namespace std {
template <>
struct hash<carta::TileCacheKey> {
    std::size_t operator()(const carta::TileCacheKey& k) const {
        return std::hash<int32_t>()(k.x) ^ (std::hash<int32_t>()(k.y) << 1);
    }
};
} // namespace std

#endif // CARTA_BACKEND_TILECACHEKEY_H
