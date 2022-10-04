/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Cache/TileCache.h"
#include "ImageGenerators/MomentGenerator.h"

using namespace carta;

TileCache* TileCache::GetTileCache() {
    return new PooledTileCache();
}

MomentGenerator* MomentGenerator::GetMomentGenerator(
    const casacore::String& filename, std::shared_ptr<casacore::ImageInterface<float>> image) {
    return new MomentGenerator(filename, image);
}
