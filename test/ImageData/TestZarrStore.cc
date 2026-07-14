/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "CommonTestUtilities.h"
#include "ImageData/ZarrStore.h"

namespace {

nlohmann::json ArrayMetadata(nlohmann::json shape, nlohmann::json data_type) {
    return {
        {"zarr_format", 3},
        {"node_type", "array"},
        {"shape", std::move(shape)},
        {"data_type", std::move(data_type)},
    };
}

nlohmann::json StorageMetadata(nlohmann::json chunk_shape, nlohmann::json codecs) {
    return {
        {"zarr_format", 3},
        {"node_type", "array"},
        {"shape", {8, 10}},
        {"data_type", "float32"},
        {"chunk_grid", {{"name", "regular"}, {"configuration", {{"chunk_shape", std::move(chunk_shape)}}}}},
        {"codecs", std::move(codecs)},
    };
}

void WriteJson(const std::filesystem::path& path, const nlohmann::json& json) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    ASSERT_TRUE(output.is_open());
    output << json;
}

class ZarrStoreSizeTest : public ::testing::Test {
protected:
    void SetUp() override {
        _store_path = TestRoot() / "data" / "generated" / "array-size.zarr";
        _outside_path = TestRoot() / "data" / "generated" / "outside-array.zarr";
        std::filesystem::remove_all(_store_path);
        std::filesystem::remove_all(_outside_path);
        std::filesystem::create_directories(_store_path);
    }

    void TearDown() override {
        std::filesystem::remove_all(_store_path);
        std::filesystem::remove_all(_outside_path);
    }

    std::filesystem::path _store_path;
    std::filesystem::path _outside_path;
};

TEST_F(ZarrStoreSizeTest, RejectsArrayPathsOutsideStore) {
    WriteJson(_store_path / "zarr.json", {{"zarr_format", 3}, {"node_type", "group"}});
    WriteJson(_store_path / "nested" / "inside" / "zarr.json", ArrayMetadata({1}, "float64"));
    WriteJson(_outside_path / "zarr.json", ArrayMetadata({1}, "float64"));
    std::filesystem::create_directory_symlink(_outside_path, _store_path / "symlink-outside");

    carta::ZarrStore store(_store_path.string());
    ASSERT_TRUE(store.Open());
    EXPECT_EQ(store.ReadArrayMetadata("nested/inside").value("node_type", ""), "array");
    EXPECT_THROW(store.ReadArrayMetadata("../outside-array.zarr"), std::runtime_error);
    EXPECT_THROW(store.ReadArrayMetadata(_outside_path.string()), std::runtime_error);
    EXPECT_THROW(store.ReadArrayMetadata("..\\outside-array.zarr"), std::runtime_error);
    EXPECT_THROW(store.ReadArrayMetadata("symlink-outside"), std::runtime_error);
}

TEST_F(ZarrStoreSizeTest, ParsesCompressorAndShardedStorageLayouts) {
    WriteJson(_store_path / "zarr.json", {{"zarr_format", 3}, {"node_type", "group"}});
    WriteJson(_store_path / "none" / "zarr.json", StorageMetadata({2, 5}, {{{"name", "bytes"}}}));
    WriteJson(_store_path / "gzip" / "zarr.json",
        StorageMetadata({2, 5}, {{{"name", "bytes"}}, {{"name", "gzip"}, {"configuration", {{"level", 4}}}}}));
    WriteJson(_store_path / "blosc" / "zarr.json",
        StorageMetadata({2, 5},
            {{{"name", "bytes"}},
                {{"name", "blosc"},
                    {"configuration", {{"cname", "zstd"}, {"clevel", 3}, {"shuffle", "bitshuffle"}}}}}));
    WriteJson(_store_path / "sharded" / "zarr.json",
        StorageMetadata({8, 10},
            {{{"name", "sharding_indexed"},
                {"configuration",
                    {{"chunk_shape", {2, 5}},
                        {"codecs", {{{"name", "bytes"}}, {{"name", "zstd"}, {"configuration", {{"level", 2}}}}}}}}}}));

    carta::ZarrStore store(_store_path.string());
    ASSERT_TRUE(store.Open());

    const auto none = store.GetStorageLayout("none");
    EXPECT_FALSE(none.sharded);
    EXPECT_EQ(none.chunk_shape, (std::vector<int64_t>{2, 5}));
    EXPECT_TRUE(none.compressor.empty());

    const auto gzip = store.GetStorageLayout("gzip");
    EXPECT_EQ(gzip.compressor, "gzip (level 4)");

    const auto blosc = store.GetStorageLayout("blosc");
    EXPECT_EQ(blosc.compressor, "Blosc + zstd (level 3, bitshuffle)");

    const auto sharded = store.GetStorageLayout("sharded");
    EXPECT_TRUE(sharded.sharded);
    EXPECT_EQ(sharded.shard_shape, (std::vector<int64_t>{8, 10}));
    EXPECT_EQ(sharded.chunk_shape, (std::vector<int64_t>{2, 5}));
    EXPECT_EQ(sharded.compressor, "zstd (level 2)");
}

TEST_F(ZarrStoreSizeTest, SumsAllConsolidatedArraysAndFixedLengthStrings) {
    const nlohmann::json fixed_length_utf32{
        {"name", "fixed_length_utf32"},
        {"configuration", {{"length_bytes", 20}}},
    };
    const nlohmann::json root_metadata{
        {"zarr_format", 3},
        {"node_type", "group"},
        {"consolidated_metadata",
            {
                {"kind", "inline"},
                {"metadata",
                    {
                        {"SKY", ArrayMetadata({2, 3}, "float32")},
                        {"FLAG_SKY", ArrayMetadata({2, 3}, "int8")},
                        {"labels", ArrayMetadata({3}, fixed_length_utf32)},
                        {"half", ArrayMetadata({2}, "float16")},
                        {"count", ArrayMetadata({1}, "uint64")},
                    }},
            }},
    };
    WriteJson(_store_path / "zarr.json", root_metadata);

    carta::ZarrStore store(_store_path.string());
    ASSERT_TRUE(store.Open());
    EXPECT_EQ(store.ComputeTotalArraySizeBytes(), 102);
}

TEST_F(ZarrStoreSizeTest, WalksOnlyZarrGroupsWithoutConsolidatedMetadata) {
    WriteJson(_store_path / "zarr.json", {{"zarr_format", 3}, {"node_type", "group"}});
    WriteJson(_store_path / "SKY" / "zarr.json", ArrayMetadata({2, 3}, "float32"));
    WriteJson(_store_path / "coordinates" / "zarr.json", {{"zarr_format", 3}, {"node_type", "group"}});
    WriteJson(_store_path / "coordinates" / "labels" / "zarr.json",
        ArrayMetadata({3}, {{"name", "fixed_length_utf32"}, {"configuration", {{"length_bytes", 20}}}}));

    // This resembles a chunk subtree. It must not be visited after SKY is identified as an array.
    WriteJson(_store_path / "SKY" / "c" / "zarr.json", ArrayMetadata({1000000}, "float64"));

    carta::ZarrStore store(_store_path.string());
    ASSERT_TRUE(store.Open());
    EXPECT_EQ(store.ComputeTotalArraySizeBytes(), 84);
}

TEST_F(ZarrStoreSizeTest, ComputesRootArraySizeAndAllowsZeroSizedDimensions) {
    WriteJson(_store_path / "zarr.json", ArrayMetadata({0, 3}, "float64"));

    carta::ZarrStore store(_store_path.string());
    ASSERT_TRUE(store.Open());
    EXPECT_EQ(store.ComputeTotalArraySizeBytes(), 0);
}

TEST_F(ZarrStoreSizeTest, FallsBackToWalkingGroupsWhenConsolidatedMetadataIsEmpty) {
    WriteJson(_store_path / "zarr.json", {{"zarr_format", 3}, {"node_type", "group"},
                                             {"consolidated_metadata", {{"kind", "inline"}, {"metadata", nlohmann::json::object()}}}});
    WriteJson(_store_path / "nested" / "zarr.json", {{"zarr_format", 3}, {"node_type", "group"}});
    WriteJson(_store_path / "nested" / "values" / "zarr.json", ArrayMetadata({2, 3}, "int16"));

    carta::ZarrStore store(_store_path.string());
    ASSERT_TRUE(store.Open());
    EXPECT_EQ(store.ComputeTotalArraySizeBytes(), 12);
}

TEST_F(ZarrStoreSizeTest, RejectsStoreWithoutArrays) {
    WriteJson(_store_path / "zarr.json", {{"zarr_format", 3}, {"node_type", "group"}});

    carta::ZarrStore store(_store_path.string());
    ASSERT_TRUE(store.Open());
    EXPECT_THROW(store.ComputeTotalArraySizeBytes(), std::runtime_error);
}

TEST_F(ZarrStoreSizeTest, RejectsMissingAndUnsupportedDataTypes) {
    const std::vector<nlohmann::json> invalid_metadata{
        {{"zarr_format", 3}, {"node_type", "array"}, {"shape", {2, 3}}},
        ArrayMetadata({2, 3}, "float128"),
        ArrayMetadata({2, 3}, {{"name", "fixed_length_utf32"}, {"configuration", {{"length_bytes", 0}}}}),
    };

    for (size_t i = 0; i < invalid_metadata.size(); ++i) {
        SCOPED_TRACE(i);
        WriteJson(_store_path / "zarr.json", invalid_metadata[i]);
        carta::ZarrStore store(_store_path.string());
        ASSERT_TRUE(store.Open());
        EXPECT_THROW(store.ComputeTotalArraySizeBytes(), std::runtime_error);
    }
}

TEST_F(ZarrStoreSizeTest, RejectsInvalidShapeDimensions) {
    const std::vector<nlohmann::json> invalid_shapes{
        {-1, 2},
        {1.5, 2},
        {std::numeric_limits<uint64_t>::max()},
    };

    for (size_t i = 0; i < invalid_shapes.size(); ++i) {
        SCOPED_TRACE(i);
        WriteJson(_store_path / "zarr.json", ArrayMetadata(invalid_shapes[i], "uint8"));
        carta::ZarrStore store(_store_path.string());
        ASSERT_TRUE(store.Open());
        EXPECT_THROW(store.ComputeTotalArraySizeBytes(), std::runtime_error);
    }
}

TEST_F(ZarrStoreSizeTest, RejectsSingleArraySizeOverflow) {
    WriteJson(_store_path / "zarr.json", ArrayMetadata({std::numeric_limits<int64_t>::max(), 2}, "uint8"));

    carta::ZarrStore store(_store_path.string());
    ASSERT_TRUE(store.Open());
    EXPECT_THROW(store.ComputeTotalArraySizeBytes(), std::runtime_error);
}

TEST_F(ZarrStoreSizeTest, RejectsTotalArraySizeOverflow) {
    const nlohmann::json root_metadata{
        {"zarr_format", 3},
        {"node_type", "group"},
        {"consolidated_metadata",
            {
                {"kind", "inline"},
                {"metadata",
                    {
                        {"large", ArrayMetadata({std::numeric_limits<int64_t>::max()}, "uint8")},
                        {"extra", ArrayMetadata({1}, "uint8")},
                    }},
            }},
    };
    WriteJson(_store_path / "zarr.json", root_metadata);

    carta::ZarrStore store(_store_path.string());
    ASSERT_TRUE(store.Open());
    EXPECT_THROW(store.ComputeTotalArraySizeBytes(), std::runtime_error);
}

} // namespace
