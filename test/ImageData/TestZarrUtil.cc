/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "CommonTestUtilities.h"
#include "ImageData/ZarrUtil.h"

namespace {

void WriteJson(const std::filesystem::path& path, const nlohmann::json& json) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    ASSERT_TRUE(output.is_open());
    output << json;
}

std::filesystem::path FixturePath(const std::string& name) {
    return TestRoot() / "data" / "images" / "zarr" / "string" / name;
}

nlohmann::json ReadMetadata(const std::filesystem::path& array_path) {
    std::ifstream input(array_path / "zarr.json");
    if (!input) {
        throw std::runtime_error("Failed to open fixture metadata " + array_path.string());
    }
    return nlohmann::json::parse(input);
}

std::vector<std::string> ReadFixture(const std::string& name) {
    const std::filesystem::path array_path = FixturePath(name);
    return carta::ReadFixedLengthUtf32StringArray(array_path, ReadMetadata(array_path));
}

class ZarrStringArraySecurityTest : public ::testing::Test {
protected:
    void SetUp() override {
        _array_path = TestRoot() / "data" / "generated" / "string-array-security";
        std::filesystem::remove_all(_array_path);
        std::filesystem::create_directories(_array_path / "c");
    }

    void TearDown() override {
        std::filesystem::remove_all(_array_path);
    }

    void CopyMetadata(const std::string& fixture) {
        std::filesystem::copy_file(FixturePath(fixture) / "zarr.json", _array_path / "zarr.json");
    }

    std::filesystem::path _array_path;
};

class ZarrImageDetectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        _root_path = TestRoot() / "data" / "generated" / "zarr-detection";
        std::filesystem::remove_all(_root_path);
        std::filesystem::create_directories(_root_path);
    }

    void TearDown() override {
        std::filesystem::remove_all(_root_path);
    }

    std::filesystem::path _root_path;
};

TEST_F(ZarrImageDetectionTest, ListsOnlySupportedSkyChildArrays) {
    const std::filesystem::path missing_path = _root_path / "missing";
    EXPECT_TRUE(carta::ListZarrImageArrays(missing_path.string()).empty());
    EXPECT_FALSE(carta::IsSupportedZarrImage(missing_path.string()));

    const std::filesystem::path no_metadata_path = _root_path / "no-metadata";
    std::filesystem::create_directories(no_metadata_path);
    EXPECT_TRUE(carta::ListZarrImageArrays(no_metadata_path.string()).empty());

    const std::filesystem::path group_path = _root_path / "group";
    WriteJson(group_path / "zarr.json", {{"zarr_format", 3}, {"node_type", "group"}});
    EXPECT_TRUE(carta::ListZarrImageArrays(group_path.string()).empty());
    EXPECT_FALSE(carta::IsSupportedZarrImage(group_path.string()));

    const std::filesystem::path root_array_path = _root_path / "root-array";
    WriteJson(root_array_path / "zarr.json", {{"zarr_format", 3}, {"node_type", "array"}});
    EXPECT_TRUE(carta::ListZarrImageArrays(root_array_path.string()).empty());
    EXPECT_FALSE(carta::IsSupportedZarrImage(root_array_path.string()));

    const std::filesystem::path supported_path = _root_path / "supported";
    WriteJson(supported_path / "zarr.json", {{"zarr_format", 3}, {"node_type", "group"}});
    WriteJson(supported_path / "SKY" / "zarr.json", {{"zarr_format", 3}, {"node_type", "array"}});
    EXPECT_EQ(carta::ListZarrImageArrays(supported_path.string()), (std::vector<std::string>{"SKY"}));
    EXPECT_TRUE(carta::IsSupportedZarrImage(supported_path.string()));
}

TEST(ZarrStringArrayTest, ReadsSingleCompressedChunkThatOverhangsArrayShape) {
    EXPECT_EQ(ReadFixture("zstd_overhang"), (std::vector<std::string>{"A", "BC"}));
}

TEST(ZarrStringArrayTest, ReadsV2ChunkKeyEncoding) {
    EXPECT_EQ(ReadFixture("v2_key"), (std::vector<std::string>{"A", "BC"}));
}

TEST(ZarrStringArrayTest, ReadsGzipCompressedChunk) {
    EXPECT_EQ(ReadFixture("gzip"), (std::vector<std::string>{"A", "BC"}));
}

TEST(ZarrStringArrayTest, ReadsBloscCompressedChunk) {
    EXPECT_EQ(ReadFixture("blosc"), (std::vector<std::string>{"A", "BC"}));
}

TEST(ZarrStringArrayTest, ReadsBigEndianUtf32) {
    EXPECT_EQ(ReadFixture("big_endian"), (std::vector<std::string>{"Ω", "🙂"}));
}

TEST(ZarrStringArrayTest, ReadsDotSeparatedDefaultChunkKey) {
    EXPECT_EQ(ReadFixture("dot_key"), (std::vector<std::string>{"A", "BC"}));
}

TEST(ZarrStringArrayTest, UsesEmptyFillValueForMissingChunk) {
    EXPECT_EQ(ReadFixture("missing_chunk"), (std::vector<std::string>{"", ""}));
}

TEST(ZarrStringArrayTest, VerifiesCrc32cOnBothSidesOfCompressor) {
    EXPECT_EQ(ReadFixture("crc_before_after_zstd"), (std::vector<std::string>{"A", "BC"}));
}

TEST(ZarrStringArrayTest, RejectsCrc32cMismatch) {
    EXPECT_THROW(ReadFixture("crc_mismatch"), std::runtime_error);
}

TEST(ZarrStringArrayTest, RejectsTruncatedChunkAndInvalidUnicode) {
    EXPECT_THROW(ReadFixture("truncated"), std::runtime_error);
    EXPECT_THROW(ReadFixture("invalid_unicode"), std::runtime_error);
}

TEST_F(ZarrStringArraySecurityTest, RejectsBloscHeaderSizeMismatch) {
    CopyMetadata("blosc");
    std::filesystem::copy_file(FixturePath("blosc") / "c" / "0", _array_path / "c" / "0");

    std::fstream chunk(_array_path / "c" / "0", std::ios::binary | std::ios::in | std::ios::out);
    ASSERT_TRUE(chunk.is_open());
    chunk.seekp(4); // Blosc bytes 4-7 contain the little-endian decompressed size.
    const char mismatched_size = 20;
    chunk.write(&mismatched_size, 1);
    chunk.close();

    EXPECT_THROW(carta::ReadFixedLengthUtf32StringArray(_array_path, ReadMetadata(_array_path)), std::runtime_error);
}

TEST(ZarrStringArrayTest, RejectsUnsupportedLayoutsAndCodecChains) {
    const std::filesystem::path array_path = FixturePath("big_endian");
    const nlohmann::json valid_metadata = ReadMetadata(array_path);
    std::vector<nlohmann::json> invalid_metadata;

    auto add_mutation = [&](auto mutate) {
        nlohmann::json metadata = valid_metadata;
        mutate(metadata);
        invalid_metadata.push_back(std::move(metadata));
    };
    add_mutation([](auto& metadata) { metadata["shape"] = {-1}; });
    add_mutation([](auto& metadata) { metadata["chunk_grid"]["configuration"]["chunk_shape"] = {0}; });
    add_mutation([](auto& metadata) {
        metadata["shape"] = {3};
        metadata["chunk_grid"]["configuration"]["chunk_shape"] = {2};
    });
    add_mutation([](auto& metadata) { metadata["chunk_key_encoding"]["name"] = "invalid"; });
    add_mutation([](auto& metadata) { metadata["codecs"] = {{{"name", "transpose"}, {"configuration", {{"order", {1}}}}}}; });
    add_mutation([](auto& metadata) { metadata["codecs"] = {{{"name", "bytes"}}, {{"name", "zstd"}}, {{"name", "gzip"}}}; });
    add_mutation([](auto& metadata) { metadata["codecs"] = {{{"name", "unknown"}}}; });
    add_mutation([](auto& metadata) { metadata["chunk_key_encoding"]["configuration"]["separator"] = "-"; });
    add_mutation([](auto& metadata) { metadata["data_type"]["configuration"]["length_bytes"] = 6; });

    for (size_t i = 0; i < invalid_metadata.size(); ++i) {
        SCOPED_TRACE(i);
        EXPECT_THROW(carta::ReadFixedLengthUtf32StringArray(array_path, invalid_metadata[i]), std::runtime_error);
    }
}

} // namespace
