/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <algorithm>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "Cache/TileCache.h"
#include "Util/Image.h"

using ::testing::_;
using ::testing::DoAll;
using ::testing::Exactly;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::SetArgReferee;

using namespace carta;

class MockFileLoader {
public:
    MOCK_METHOD(bool, GetChunk,
        (std::vector<float> & data, int& data_width, int& data_height, int min_x, int min_y, int z, int stokes, std::mutex& image_mutex),
        ());
};

std::vector<float> TestChunk(std::vector<float> fill) {
    std::vector<float> chunk;
    chunk.resize(CHUNK_SIZE * CHUNK_SIZE);
    std::vector<float>::iterator it = chunk.begin();
    for (uint row_offset = 0; row_offset < 2; row_offset++) {
        for (uint row = 0; row < TILE_SIZE; row++) {
            for (uint column_offset = 0; column_offset < 2; column_offset++) {
                float& val = fill[2 * row_offset + column_offset];
                for (uint column = 0; column < TILE_SIZE; column++) {
                    // TODO can this be done more efficiently?
                    *it = val;
                    it++;
                }
            }
        }
    }
    return chunk;
};

std::vector<float> ZeroChunk() {
    std::vector<float> chunk;
    chunk.resize(CHUNK_SIZE * CHUNK_SIZE, 0);
    return chunk;
};

bool CheckFill(TilePtr tile, float fill) {
    return std::all_of(tile->cbegin(), tile->cend(), [&](float v) { return v == fill; });
}

TEST(TileCacheTest, TestChunkKey) {
    ASSERT_EQ(TileCache::ChunkKey(TileCache::Key(0, 0)), TileCache::Key(0, 0));
    ASSERT_EQ(TileCache::ChunkKey(TileCache::Key(256, 0)), TileCache::Key(0, 0));
    ASSERT_EQ(TileCache::ChunkKey(TileCache::Key(0, 256)), TileCache::Key(0, 0));
    ASSERT_EQ(TileCache::ChunkKey(TileCache::Key(256, 256)), TileCache::Key(0, 0));

    // This is not how the function is used, but it should still give the correct answer
    ASSERT_EQ(TileCache::ChunkKey(TileCache::Key(5, 5)), TileCache::Key(0, 0));
}

TEST(TileCacheTest, TestPeek) {
    TileCache cache(7);

    auto loader = std::make_shared<MockFileLoader>();
    std::mutex mutex;

    // Empty test chunk
    auto test_chunk = ZeroChunk();

    EXPECT_CALL(*loader, GetChunk(_, _, _, 0, 0, 0, 0, _))
        .Times(Exactly(1))
        .WillOnce(DoAll(SetArgReferee<0>(test_chunk), SetArgReferee<1>(CHUNK_SIZE), SetArgReferee<2>(CHUNK_SIZE), Return(true)));

    // Requires a new chunk read
    auto tile_1 = cache.Get<MockFileLoader>(TileCache::Key(0, 0), loader, mutex);

    // This should retrieve the same tile
    ASSERT_EQ(cache.Peek(TileCache::Key(0, 0)), tile_1);
    // This should retrieve nullptr, because it's not in the cache and will not be fetched
    ASSERT_EQ(cache.Peek(TileCache::Key(512, 512)), nullptr);
}

TEST(TileCacheTest, TestGetOneChunk) {
    TileCache cache(7);

    auto loader = std::make_shared<MockFileLoader>();
    std::mutex mutex;

    // Test chunk has each quadrant filled with the same value
    auto test_chunk = TestChunk({1, 2, 3, 4});

    EXPECT_CALL(*loader, GetChunk(_, _, _, 0, 0, 0, 0, _))
        .Times(Exactly(1))
        .WillOnce(DoAll(SetArgReferee<0>(test_chunk), SetArgReferee<1>(CHUNK_SIZE), SetArgReferee<2>(CHUNK_SIZE), Return(true)));

    // Requires a new chunk read
    auto tile_1 = cache.Get<MockFileLoader>(TileCache::Key(0, 0), loader, mutex);
    // Already in cache
    auto tile_2 = cache.Get<MockFileLoader>(TileCache::Key(256, 0), loader, mutex);
    // Already in cache
    auto tile_3 = cache.Get<MockFileLoader>(TileCache::Key(0, 256), loader, mutex);
    // Already in cache
    auto tile_4 = cache.Get<MockFileLoader>(TileCache::Key(256, 256), loader, mutex);

    // check tile contents
    ASSERT_TRUE(CheckFill(tile_1, 1));
    ASSERT_TRUE(CheckFill(tile_2, 2));
    ASSERT_TRUE(CheckFill(tile_3, 3));
    ASSERT_TRUE(CheckFill(tile_4, 4));
}

TEST(TileCacheTest, TestTileEviction) {
    TileCache cache(7);

    auto loader = std::make_shared<MockFileLoader>();
    std::mutex mutex;

    // Empty test chunk
    auto test_chunk = ZeroChunk();

    InSequence seq;

    EXPECT_CALL(*loader, GetChunk(_, _, _, 0, 0, 0, 0, _))
        .Times(Exactly(1))
        .WillOnce(DoAll(SetArgReferee<0>(test_chunk), SetArgReferee<1>(CHUNK_SIZE), SetArgReferee<2>(CHUNK_SIZE), Return(true)));
    EXPECT_CALL(*loader, GetChunk(_, _, _, 512, 512, 0, 0, _))
        .Times(Exactly(1))
        .WillOnce(DoAll(SetArgReferee<0>(test_chunk), SetArgReferee<1>(CHUNK_SIZE), SetArgReferee<2>(CHUNK_SIZE), Return(true)));
    EXPECT_CALL(*loader, GetChunk(_, _, _, 0, 0, 0, 0, _))
        .Times(Exactly(1))
        .WillOnce(DoAll(SetArgReferee<0>(test_chunk), SetArgReferee<1>(CHUNK_SIZE), SetArgReferee<2>(CHUNK_SIZE), Return(true)));

    // Requires a new chunk read; this adds 4 tiles
    auto tile_1 = cache.Get<MockFileLoader>(TileCache::Key(0, 0), loader, mutex);
    // Requires a new chunk read; this adds 4 tiles and evicts tile 1 (oldest because currently chunk tiles are added/updated in order)
    auto tile_5 = cache.Get<MockFileLoader>(TileCache::Key(512, 512), loader, mutex);
    // Was evicted; requires a new chunk read; this adds back first 4 tiles and evicts tile 5
    tile_1 = cache.Get<MockFileLoader>(TileCache::Key(0, 0), loader, mutex);
    // Should still be in cache
    auto tile_6 = cache.Get<MockFileLoader>(TileCache::Key(768, 512), loader, mutex);
}

TEST(TileCacheTest, TestReset) {
    TileCache cache(7);

    auto loader = std::make_shared<MockFileLoader>();
    std::mutex mutex;

    // Empty test chunk
    auto test_chunk = ZeroChunk();

    InSequence seq;

    EXPECT_CALL(*loader, GetChunk(_, _, _, 0, 0, 0, 0, _))
        .Times(Exactly(1))
        .WillOnce(DoAll(SetArgReferee<0>(test_chunk), SetArgReferee<1>(CHUNK_SIZE), SetArgReferee<2>(CHUNK_SIZE), Return(true)));
    EXPECT_CALL(*loader, GetChunk(_, _, _, 0, 0, 10, 1, _))
        .Times(Exactly(1))
        .WillOnce(DoAll(SetArgReferee<0>(test_chunk), SetArgReferee<1>(CHUNK_SIZE), SetArgReferee<2>(CHUNK_SIZE), Return(true)));

    // Requires a new chunk read
    auto tile_1 = cache.Get<MockFileLoader>(TileCache::Key(0, 0), loader, mutex);

    // Change channel and Stokes
    cache.Reset(10, 1, 7);

    // Same tile, after reset, requires a new chunk read
    tile_1 = cache.Get<MockFileLoader>(TileCache::Key(0, 0), loader, mutex);
}

TEST(TileCacheKeyTest, TestOperators) {
    auto a = TileCache::Key(3, 4);
    auto b = TileCache::Key(3, 4);
    auto c = TileCache::Key(4, 3);

    ASSERT_EQ(a, b);
    ASSERT_NE(a, c);

    ASSERT_FALSE(a != b);
    ASSERT_FALSE(a == c);
}

TEST(TileCacheKeyTest, TestHash) {
    auto a = TileCache::Key(3, 4);
    auto b = TileCache::Key(4, 3);

    ASSERT_NE(std::hash<TileCache::Key>()(a), std::hash<TileCache::Key>()(b));
}

TEST(TilePoolTest, TestCapacity) {
    // Default capacity of 4
    auto pool = std::make_shared<TilePool>();
    ASSERT_FALSE(pool->Full());

    {
        // Create 4 tiles
        auto tile_1 = pool->Pull();
        auto tile_2 = pool->Pull();
        auto tile_3 = pool->Pull();
        auto tile_4 = pool->Pull();
    }

    // 4 tiles returned to pool on deletion

    // The pool is now full
    ASSERT_TRUE(pool->Full());
}

TEST(TilePoolTest, TestReuse) {
    // Default capacity of 4
    auto pool = std::make_shared<TilePool>();

    {
        // Create a tile
        auto tile_1 = pool->Pull();
        std::fill(tile_1->begin(), tile_1->end(), 1);
    }

    // Tile returned to pool

    // Get a tile from the pool
    auto tile_1 = pool->Pull();
    // Should be the tile that was just returned
    ASSERT_TRUE(CheckFill(tile_1, 1));
}
