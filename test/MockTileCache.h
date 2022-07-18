/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_TEST_MOCKTILECACHE_H
#define CARTA_TEST_MOCKTILECACHE_H

#include "gmock/gmock.h"

#include "Cache/TileCache.h"

namespace carta {

class MockTileCache : public TileCache {
public:
    MOCK_METHOD(TilePtr, Peek, (Key key), (override));
    MOCK_METHOD(TilePtr, Get, (Key key, std::shared_ptr<FileLoader> loader, std::mutex& image_mutex), (override));
    MOCK_METHOD(void, Reset, (int32_t z, int32_t stokes, int capacity), (override));
};

} // namespace carta

#endif // CARTA_TEST_MOCKTILECACHE_H
