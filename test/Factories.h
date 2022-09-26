/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_TEST_TESTFACTORIES_H
#define CARTA_TEST_TESTFACTORIES_H

#include <queue>
#include "Cache/TileCache.h"

namespace carta {

struct Factories {
    static std::queue<TileCache*> _mock_tile_caches;
};

} // namespace carta

#endif // CARTA_TEST_TESTFACTORIES_H
