/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <zstd.h>
#include <nlohmann/json.hpp>

#include "CommonTestUtilities.h"
#include "ImageData/ZarrUtil.h"

namespace {

class ZarrStringArrayTest : public ::testing::Test {
protected:
    void SetUp() override {
        _array_path = TestRoot() / "data" / "generated" / "string-array";
        std::filesystem::remove_all(_array_path);
        std::filesystem::create_directories(_array_path / "c");
    }

    void TearDown() override {
        std::filesystem::remove_all(_array_path);
    }

    std::filesystem::path _array_path;
};

TEST_F(ZarrStringArrayTest, ReadsSingleCompressedChunkThatOverhangsArrayShape) {
    const nlohmann::json metadata{
        {"zarr_format", 3},
        {"node_type", "array"},
        {"shape", {2}},
        {"data_type", {{"name", "fixed_length_utf32"}, {"configuration", {{"length_bytes", 8}}}}},
        {"chunk_grid", {{"name", "regular"}, {"configuration", {{"chunk_shape", {4}}}}}},
        {"chunk_key_encoding", {{"name", "default"}, {"configuration", {{"separator", "/"}}}}},
        {"codecs", {{{"name", "bytes"}, {"configuration", {{"endian", "little"}}}}, {{"name", "zstd"}, {"configuration", {{"level", 1}}}}}},
    };

    // Boundary chunks retain the full chunk shape. Only the first two elements are in the array.
    const std::vector<uint32_t> code_points{'A', 0, 'B', 'C', 'D', 0, 'E', 0};
    const size_t source_size = code_points.size() * sizeof(uint32_t);
    std::vector<uint8_t> compressed(ZSTD_compressBound(source_size));
    const size_t compressed_size = ZSTD_compress(compressed.data(), compressed.size(), code_points.data(), source_size, 1);
    ASSERT_FALSE(ZSTD_isError(compressed_size));

    std::ofstream chunk_file(_array_path / "c" / "0", std::ios::binary);
    ASSERT_TRUE(chunk_file.is_open());
    chunk_file.write(reinterpret_cast<const char*>(compressed.data()), static_cast<std::streamsize>(compressed_size));
    chunk_file.close();

    EXPECT_EQ(carta::ReadFixedLengthUtf32StringArray(_array_path, metadata), (std::vector<std::string>{"A", "BC"}));
}

TEST_F(ZarrStringArrayTest, ReadsV2ChunkKeyEncoding) {
    const nlohmann::json metadata{
        {"zarr_format", 3},
        {"node_type", "array"},
        {"shape", {2}},
        {"data_type", {{"name", "fixed_length_utf32"}, {"configuration", {{"length_bytes", 8}}}}},
        {"chunk_grid", {{"name", "regular"}, {"configuration", {{"chunk_shape", {2}}}}}},
        {"chunk_key_encoding", {{"name", "v2"}}},
        {"codecs", {{{"name", "bytes"}, {"configuration", {{"endian", "little"}}}}}},
    };

    const std::vector<uint32_t> code_points{'A', 0, 'B', 'C'};
    std::ofstream chunk_file(_array_path / "0", std::ios::binary);
    ASSERT_TRUE(chunk_file.is_open());
    chunk_file.write(
        reinterpret_cast<const char*>(code_points.data()), static_cast<std::streamsize>(code_points.size() * sizeof(uint32_t)));
    chunk_file.close();

    EXPECT_EQ(carta::ReadFixedLengthUtf32StringArray(_array_path, metadata), (std::vector<std::string>{"A", "BC"}));
}

} // namespace
