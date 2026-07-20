/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "Session/Session.h"

using namespace carta;

TEST(ChannelMapTileCacheTest, RequiresAnExactRequestMatch) {
    ChannelMapTileRequestKey request{1, 3, 0, CARTA::CompressionType::ZFP, 11.0f, {1, 2, 3}};
    EXPECT_EQ(request, request);

    auto changed = request;
    changed.channel = 4;
    EXPECT_FALSE(request == changed);

    changed = request;
    changed.stokes = 1;
    EXPECT_FALSE(request == changed);

    changed = request;
    changed.compression_quality = 12.0f;
    EXPECT_FALSE(request == changed);

    changed = request;
    changed.tiles = {1, 3};
    EXPECT_FALSE(request == changed);
}
