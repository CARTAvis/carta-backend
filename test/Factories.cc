/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Factories.h"

using namespace carta;

TileCache* Factories::_mock_tile_cache = nullptr;

void Factories::Reset() {
    _mock_tile_cache = nullptr;
}

TileCache* TileCache::GetTileCache() {
    if (Factories::_mock_tile_cache) {
        return Factories::_mock_tile_cache;
    }
    return new PooledTileCache();
}
