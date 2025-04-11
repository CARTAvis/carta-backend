/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <chrono>
#include <random>

#include <gtest/gtest.h>

#include "DataStream/Tile.h"

using namespace carta;

TEST(TileEncodingTest, InvalidEncoding) {
    std::vector<std::tuple<int32_t, int32_t, int32_t >> invalid_cases = {
        {-1, 0, 0}, {0, -1, 0}, {0, 0, -1},                         // negative values
        {0, 0, -1}, {0, 0, 13},                                     // invalid layer
        {4096, 0, 12}, {0, 4096, 12}, {-1, 0, 12}, {0, -1, 12},     // x/y too large
        {1 << 10, 0, 10}, {0, 1 << 10, 10},                         // x/y on upper edge
        {0, 1024, 10}, {0, 256, 8}, {0, 4, 2}                       // out of bounds
    };

    for (auto& [x, y, layer] : invalid_cases) {
        int32_t result = Tile::Encode(x, y, layer);
        EXPECT_EQ(result, -1) << "Expected -1 for invalid (x=" << x << ", y=" << y << ", layer=" << layer << ")";
    }
}

TEST(TileEncodingTest, RoundTrip) {
    std::random_device rd;
    std::mt19937 mt(rd());
    std::uniform_int_distribution<> layer_random(0, 12);
    std::uniform_real_distribution<float> float_random(0, 1);

    for (int i = 0; i < 10000; i++) {
        int32_t layer = layer_random(mt);
        int32_t layer_width = 1 << layer;
        int32_t x = static_cast<int32_t>(float_random(mt) * layer_width);
        int32_t y = static_cast<int32_t>(float_random(mt) * layer_width);

        int32_t encoded = Tile::Encode(x, y, layer);
        ASSERT_NE(encoded, -1) << "Encode failed for valid values: x=" << x << " y=" << y << " layer=" << layer;

        Tile decoded = Tile::Decode(encoded);

        ASSERT_EQ(decoded.x, x) << "Mismatch at iteration " << i << ": decoded.x=" << decoded.x << " expected=" << x;
        ASSERT_EQ(decoded.y, y) << "Mismatch at iteration " << i << ": decoded.y=" << decoded.y << " expected=" << y;
        ASSERT_EQ(decoded.layer, layer) << "Mismatch at iteration " << i << ": decoded.layer=" << decoded.layer << " expected=" << layer;
    }
}

TEST(TileEncodingTest, BoundaryEncodingDecoding) {
    for (int32_t layer = 0; layer <= 12; ++layer) {
        int32_t width = 1 << layer;
        std::vector<std::pair<int32_t, int32_t>> points = {
            {0, 0},
            {width - 1, 0},
            {0, width - 1},
            {width - 1, width - 1}
        };

        for (const auto& [x, y] : points) {
            int32_t encoded = Tile::Encode(x, y, layer);
            ASSERT_NE(encoded, -1) << "Failed to encode boundary point";

            Tile decoded = Tile::Decode(encoded);
            EXPECT_EQ(decoded.x, x);
            EXPECT_EQ(decoded.y, y);
            EXPECT_EQ(decoded.layer, layer);
        }
    }
}

TEST(TileEncodingTest, LayerToMipConversion) {
    int32_t img_width = 1024;
    int32_t img_height = 512;
    int32_t tile_width = 256;
    int32_t tile_height = 256;

    // Manually calculated expected mips
    std::vector<std::pair<int, int>> layer_to_expected_mip = {
        {0, 4}, {1, 2}, {2, 1}
    };

    for (const auto& [layer, expected_mip] : layer_to_expected_mip) {
        int32_t mip = Tile::LayerToMip(layer, img_width, img_height, tile_width, tile_height);
        EXPECT_EQ(mip, expected_mip) << "Unexpected mip for layer " << layer;
    }
}

TEST(TileEncodingTest, MipLayerRoundTrip) {
    int32_t img_width = 1024;
    int32_t img_height = 1024;
    int32_t tile_width = 256;
    int32_t tile_height = 256;

    for (int layer = 0; layer <= 4; ++layer) {
        int mip = Tile::LayerToMip(layer, img_width, img_height, tile_width, tile_height);
    
        // Skip invalid mip-to-layer mappings
        if (mip <= 0) continue;
    
        int roundtrip_layer = Tile::MipToLayer(mip, img_width, img_height, tile_width, tile_height);
        EXPECT_EQ(roundtrip_layer, layer) << "Round-trip failed: layer " << layer << " → mip " << mip << " → layer " << roundtrip_layer;
    }
}


#ifdef COMPILE_PERFORMANCE_TESTS

TEST(TileEncoding, PerformanceTestEncoding) {
    int32_t layer = 12;
    int64_t encoded_val = 0;
    auto t_start = std::chrono::high_resolution_clock::now();
    for (auto i = 0; i < 1000; i++) {
        for (auto j = 0; j < 1000; j++) {
            encoded_val += Tile::Encode(i, j, layer);
        }
    }
    auto t_end = std::chrono::high_resolution_clock::now();
    float dt = std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_start).count() / 1000.0f;
    ASSERT_EQ(encoded_val, 203373043500000);
    ASSERT_LT(dt, 2.0f);
}

TEST(TileEncoding, PerformanceTestDecoding) {
    int32_t layer = 12;
    int32_t layer_width = 1 << layer;
    int64_t encoded_val = 0;
    int64_t counter = 0;

    auto t_start = std::chrono::high_resolution_clock::now();

    for (auto i = 0; i < 1000; i++) {
        for (auto j = 0; j < 1000; j++) {
            counter += Tile::Decode(encoded_val).x;
            encoded_val++;
        }
        encoded_val += layer_width;
    }

    auto t_end = std::chrono::high_resolution_clock::now();
    float dt = std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_start).count() / 1000.0f;
    ASSERT_EQ(counter, 2046486240);
    ASSERT_LT(dt, 2.0f);
}

#endif
