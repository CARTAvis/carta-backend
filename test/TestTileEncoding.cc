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
    std::vector<std::tuple<int32_t, int32_t, int32_t>> invalid_cases = {
        {-1, 0, 0}, {0, -1, 0}, {0, 0, -1},                     // negative values
        {0, 0, -1}, {0, 0, 13},                                 // invalid layer
        {4096, 0, 12}, {0, 4096, 12}, {-1, 0, 12}, {0, -1, 12}, // x/y too large
        {1 << 10, 0, 10}, {0, 1 << 10, 10},                     // x/y on upper edge
        {0, 1024, 10}, {0, 256, 8}, {0, 4, 2}                   // out of bounds
    };

    for (auto& [x, y, layer] : invalid_cases) {
        int32_t result = Tile::Encode(x, y, layer);
        EXPECT_EQ(result, -1) << "Expected -1 for invalid (x=" << x << ", y=" << y << ", layer=" << layer << ")";
    }
}

TEST(TileEncodingTest, DecodeInvalidEncodedValue) {
    std::vector<int32_t> invalid_encoded = {-1, -123456789};
    for (auto val : invalid_encoded) {
        Tile decoded = Tile::Decode(val);
        // We can't assert failure, but we can log and verify it still returns something parseable
        EXPECT_GE(decoded.x, 0);
        EXPECT_GE(decoded.y, 0);
        EXPECT_GE(decoded.layer, 0);
    }
}

TEST(TileEncodingTest, RoundTrip) {
    std::random_device rd;
    std::mt19937 mt(rd());
    std::uniform_int_distribution<> layer_random(0, 12);
    std::uniform_real_distribution<float> float_random(0, 1);

    for (int i = 0; i < 1000; i++) {
        int32_t layer = layer_random(mt);
        int32_t layer_width = 1 << layer;
        int32_t x = std::min(static_cast<int32_t>(float_random(mt) * layer_width), layer_width - 1);
        int32_t y = std::min(static_cast<int32_t>(float_random(mt) * layer_width), layer_width - 1);

        int32_t encoded = Tile::Encode(x, y, layer);
        ASSERT_NE(encoded, -1) << "Encode failed for valid values: x=" << x << " y=" << y << " layer=" << layer;

        Tile decoded = Tile::Decode(encoded);

        ASSERT_GE(x, 0);
        ASSERT_LT(x, layer_width);
        ASSERT_GE(y, 0);
        ASSERT_LT(y, layer_width);

        ASSERT_EQ(decoded.x, x) << "Mismatch at iteration " << i << ": decoded.x=" << decoded.x << " expected=" << x;
        ASSERT_EQ(decoded.y, y) << "Mismatch at iteration " << i << ": decoded.y=" << decoded.y << " expected=" << y;
        ASSERT_EQ(decoded.layer, layer) << "Mismatch at iteration " << i << ": decoded.layer=" << decoded.layer << " expected=" << layer;
    }
}

TEST(TileEncodingTest, RoundTripFloatPrecisionEdgeCase) {
    int32_t layer = 6; // 2^6 = 64
    int32_t layer_width = 1 << layer;

    // Explicitly test x = layer_width, which should be invalid
    int32_t x = layer_width;
    int32_t y = 10;

    int32_t encoded = Tile::Encode(x, y, layer);
    EXPECT_EQ(encoded, -1) << "Expected Encode to return -1 for out-of-bounds x";

    // Flip x/y to check y overflow
    encoded = Tile::Encode(10, layer_width, layer);
    EXPECT_EQ(encoded, -1) << "Expected Encode to return -1 for out-of-bounds y";
}

TEST(TileEncodingTest, LayerToMipConversion) {
    int32_t img_width = 1024;
    int32_t img_height = 512;
    int32_t tile_width = 256;
    int32_t tile_height = 256;

    // Manually calculated expected mips
    std::vector<std::pair<int, int>> layer_to_expected_mip = {{0, 4}, {1, 2}, {2, 1}};

    for (const auto& [layer, expected_mip] : layer_to_expected_mip) {
        int32_t mip = Tile::LayerToMip(layer, img_width, img_height, tile_width, tile_height);
        EXPECT_EQ(mip, expected_mip) << "Unexpected mip for layer " << layer;
    }
}

TEST(TileEncodingTest, MaxLayerRoundTrip) {
    int32_t layer = 12;
    int32_t width = 1 << layer;

    for (int x = width - 10; x < width; ++x) {
        for (int y = width - 10; y < width; ++y) {
            int32_t encoded = Tile::Encode(x, y, layer);
            ASSERT_NE(encoded, -1);

            Tile decoded = Tile::Decode(encoded);
            EXPECT_EQ(decoded.x, x);
            EXPECT_EQ(decoded.y, y);
            EXPECT_EQ(decoded.layer, layer);
        }
    }
}

TEST(TileEncodingTest, EdgeAndBoundaryCoordinates) {
    // Test corners for all layers
    for (int32_t layer = 0; layer <= 12; ++layer) {
        int32_t width = 1 << layer;
        std::vector<std::pair<int32_t, int32_t>> corners = {{0, 0}, {width - 1, 0}, {0, width - 1}, {width - 1, width - 1}};
        for (const auto& [x, y] : corners) {
            int32_t encoded = Tile::Encode(x, y, layer);
            ASSERT_NE(encoded, -1) << "Failed to encode boundary point";

            Tile decoded = Tile::Decode(encoded);
            EXPECT_EQ(decoded.x, x);
            EXPECT_EQ(decoded.y, y);
            EXPECT_EQ(decoded.layer, layer);
        }
    }

    // Test additional edge coordinates for layers 0 and 12
    std::vector<int32_t> edge_coords = {0, 2047, 2048, 4095};
    std::vector<int32_t> layers = {0, 12};
    for (int32_t layer : layers) {
        int32_t layer_width = 1 << layer;
        for (int32_t x : edge_coords) {
            for (int32_t y : edge_coords) {
                if (x >= layer_width || y >= layer_width)
                    continue;
                int32_t encoded = Tile::Encode(x, y, layer);
                ASSERT_NE(encoded, -1) << "Encoding failed for x=" << x << ", y=" << y << ", layer=" << layer;

                Tile tile = Tile::Decode(encoded);
                EXPECT_EQ(tile.x, x) << "Mismatch at x=" << x << ", y=" << y << ", layer=" << layer;
                EXPECT_EQ(tile.y, y) << "Mismatch at x=" << x << ", y=" << y << ", layer=" << layer;
                EXPECT_EQ(tile.layer, layer) << "Mismatch at x=" << x << ", y=" << y << ", layer=" << layer;
            }
        }
    }
}

TEST(TileEncodingTest, LayerToMipEdgeCasesAndSweep) {
    int32_t tile_width = 256;
    int32_t tile_height = 256;
    int32_t layer = 0;

    // Automated checks for specific edge cases
    struct EdgeCase {
        int32_t img_width;
        int32_t expected_mip;
    };
    std::vector<EdgeCase> edge_cases = {
        {511, 2}, // 511px → ceil(511/256)=2 tiles → log2(2)=1 → pow(2,1)=2
        {513, 4}  // 513px → ceil(513/256)=3 tiles → log2(3)=1.58 → ceil=2 → pow(2,2)=4
    };
    int32_t img_height = 256;
    for (const auto& ec : edge_cases) {
        int32_t mip = Tile::LayerToMip(layer, ec.img_width, img_height, tile_width, tile_height);
        EXPECT_EQ(mip, ec.expected_mip) << "Mip should be " << ec.expected_mip << " for " << ec.img_width << "px width";
    }

    // Sweep logging for manual inspection
    int last_mip = -1;
    for (int img_width = 450; img_width <= 550; ++img_width) {
        int mip = Tile::LayerToMip(layer, img_width, img_height, tile_width, tile_height);
        if (mip != last_mip) {
            last_mip = mip;
        }
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
