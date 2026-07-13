/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <filesystem>
#include <fstream>
#include <utility>

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
        std::filesystem::remove_all(_store_path);
        std::filesystem::create_directories(_store_path);
    }

    void TearDown() override {
        std::filesystem::remove_all(_store_path);
    }

    std::filesystem::path _store_path;
};

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

} // namespace
