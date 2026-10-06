/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

#include <casacore/casa/Exceptions/Error.h>

#include "FileList/FileInfoLoader.h"
#include "ImageData/CartaZarrImage.h"
#include "ImageData/ZarrStores.h"
#include "Util/Casacore.h"

using namespace carta;

namespace fs = std::filesystem;

namespace {

const fs::path kZarrFixture{ZARR_PIXEL_FIXTURE};

std::string ReadText(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void WriteText(const fs::path& path, const std::string& text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << text;
}

} // namespace

class ZarrStoresTest : public ::testing::Test {
public:
    void SetUp() override {
        if (!fs::exists(kZarrFixture)) {
            GTEST_SKIP() << "carta-zarr pixel fixture not found at " << kZarrFixture;
        }
        _scratch = fs::temp_directory_path() / ("carta_zarr_stores_" + std::to_string(::getpid()));
        fs::remove_all(_scratch);
        fs::create_directories(_scratch);
        ForgetEverything();
    }

    void TearDown() override {
        ForgetEverything();
        std::error_code error;
        fs::remove_all(_scratch, error);
    }

    // A copy of the pixel fixture, an image store
    fs::path AStore(const std::string& name = "store.zarr") {
        const auto store = _scratch / name;
        fs::copy(kZarrFixture, store, fs::copy_options::recursive);
        return store;
    }

    // An XRADIO image store without the coordinate system every image needs
    fs::path AMalformedStore() {
        const auto store = AStore("malformed.zarr");
        auto text = ReadText(store / "zarr.json");
        const std::string key = "\"coordinate_system_info\"";
        const auto at = text.find(key);
        EXPECT_NE(at, std::string::npos);
        text.replace(at, key.size(), "\"not_coordinate_system_info\"");
        WriteText(store / "zarr.json", text);
        return store;
    }

    // A valid Zarr v3 group that is not an image
    fs::path AZarrOfSomethingElse() {
        const auto store = _scratch / "other.zarr";
        fs::create_directories(store);
        WriteText(store / "zarr.json", R"({"zarr_format": 3, "node_type": "group", "attributes": {"type": "visibilities"}})");
        return store;
    }

    // Root metadata that does not parse
    fs::path AnUnparseableRoot() {
        const auto store = _scratch / "unparseable.zarr";
        fs::create_directories(store);
        WriteText(store / "zarr.json", "{ this is not json");
        return store;
    }

    static void ForgetEverything() {
        ZarrStores::Instance().Clear();
    }

    // Append to a chunk file several levels below the root, which changes no directory timestamp
    static void GrowAChunk(const fs::path& store, std::size_t bytes) {
        for (const auto& entry : fs::recursive_directory_iterator(store / "SKY" / "c")) {
            if (entry.is_regular_file()) {
                std::ofstream stream(entry.path(), std::ios::binary | std::ios::app);
                stream << std::string(bytes, 'x');
                return;
            }
        }
        ADD_FAILURE() << "no chunk file in " << store;
    }

    static int64_t FileInfoSize(const fs::path& store) {
        CARTA::FileInfo file_info;
        FileInfoLoader(store.string(), CARTA::FileType::ZARR).FillFileInfo(file_info);
        return file_info.size();
    }

    fs::path _scratch;
};

TEST_F(ZarrStoresTest, AStoreIsOpenedOnceAndOffersItsImages) {
    const auto look = ZarrStores::Instance().Look(AStore().string());
    EXPECT_TRUE(look->is_zarr);
    EXPECT_TRUE(look->dataset.has_value());
    ASSERT_EQ(look->offered.size(), 1);
    EXPECT_EQ(look->offered.front(), "SKY");
    EXPECT_TRUE(look->why_not_openable.empty());
}

// A malformed image store is listed as an image, and selecting or opening it says why it is broken
TEST_F(ZarrStoresTest, AMalformedStoreIsAnImageThatSaysWhyItWillNotOpen) {
    const auto store = AMalformedStore().string();

    const auto look = ZarrStores::Instance().Look(store);
    EXPECT_TRUE(look->is_zarr);
    EXPECT_FALSE(look->dataset.has_value());
    EXPECT_NE(look->why_not_openable.find("coordinate_system_info"), std::string::npos) << look->why_not_openable;

    // Listed as an image
    std::string message;
    EXPECT_EQ(FolderImageType(store, message), CARTA::FileType::ZARR);

    // Selecting it says why
    CARTA::FileInfo file_info;
    FileInfoLoader loader(store);
    EXPECT_FALSE(loader.FillFileInfo(file_info));
    EXPECT_EQ(file_info.type(), CARTA::FileType::ZARR);
    EXPECT_NE(loader.Message().find(look->why_not_openable), std::string::npos) << loader.Message();

    // So does opening it
    try {
        CartaZarrImage image(store);
        FAIL() << "a malformed store opened";
    } catch (const casacore::AipsError& error) {
        EXPECT_NE(std::string(error.getMesg()).find(look->why_not_openable), std::string::npos) << error.getMesg();
    }
}

TEST_F(ZarrStoresTest, AZarrStoreOfSomethingElseIsNotAnImage) {
    const auto store = AZarrOfSomethingElse().string();
    const auto look = ZarrStores::Instance().Look(store);
    EXPECT_FALSE(look->is_zarr);
    EXPECT_FALSE(look->dataset.has_value());

    std::string message;
    EXPECT_NE(FolderImageType(store, message), CARTA::FileType::ZARR);
}

// Root metadata that does not parse is not recognised as an image
TEST_F(ZarrStoresTest, AnUnparseableRootIsNotAnImage) {
    const auto store = AnUnparseableRoot().string();
    const auto look = ZarrStores::Instance().Look(store);
    EXPECT_FALSE(look->is_zarr);

    std::string message;
    EXPECT_NE(FolderImageType(store, message), CARTA::FileType::ZARR);
}

TEST_F(ZarrStoresTest, AFileIsNotAStore) {
    const auto file = _scratch / "zarr.json";
    fs::copy_file(kZarrFixture / "zarr.json", file);
    EXPECT_FALSE(ZarrStores::Instance().Look(file.string())->is_zarr);
    EXPECT_FALSE(ZarrStores::Instance().Look((_scratch / "missing.zarr").string())->is_zarr);
}

TEST_F(ZarrStoresTest, ALookIsKeptAndSharedByEveryoneWhoAsks) {
    ZarrStores stores(4, std::chrono::seconds(30));
    const auto store = AStore().string();

    const auto first = stores.Look(store);
    EXPECT_EQ(stores.Look(store), first);
    // The same path spelled differently
    EXPECT_EQ(stores.Look(store + "/"), first);

    stores.Clear();
    EXPECT_NE(stores.Look(store), first);
}

// A store whose directory timestamp changed is opened again
TEST_F(ZarrStoresTest, AChangedDirectoryIsLookedAtAgain) {
    ZarrStores stores(4, std::chrono::seconds(30));
    const auto store = AStore();

    const auto first = stores.Look(store.string());
    fs::create_directory(store / "NEW_VARIABLE");
    const auto second = stores.Look(store.string());
    EXPECT_NE(second, first);
    EXPECT_EQ(stores.Look(store.string()), second);
}

TEST_F(ZarrStoresTest, ALookIsKeptOnlyForTheTtl) {
    ZarrStores stores(4, std::chrono::seconds(0));
    const auto store = AStore().string();

    const auto first = stores.Look(store);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    EXPECT_NE(stores.Look(store), first);
}

TEST_F(ZarrStoresTest, TheLeastRecentlyUsedLookIsForgotten) {
    ZarrStores stores(2, std::chrono::seconds(30));
    const auto a = AStore("a.zarr").string();
    const auto b = AStore("b.zarr").string();
    const auto c = AStore("c.zarr").string();

    const auto first_a = stores.Look(a);
    const auto first_b = stores.Look(b);
    // Asking about a makes b the oldest.
    EXPECT_EQ(stores.Look(a), first_a);
    stores.Look(c);

    EXPECT_EQ(stores.Look(a), first_a);
    EXPECT_NE(stores.Look(b), first_b);
}

// A file type is sent as its number, so existing numbers keep their meaning
TEST(FileTypeNumbers, ANumberKeepsTheTypeItWasGiven) {
    EXPECT_EQ(static_cast<int>(CARTA::FileType::MIRIAD), 5);
    EXPECT_EQ(static_cast<int>(CARTA::FileType::UNKNOWN), 6);
    EXPECT_EQ(static_cast<int>(CARTA::FileType::ZARR), 7);
}

// Listing, selecting and opening a store measure its size once
TEST_F(ZarrStoresTest, ASizeIsMeasuredOnceForALook) {
    const auto store = AStore();
    CARTA::FileInfo file_info;
    FileInfoLoader loader(store.string());
    EXPECT_TRUE(loader.FillFileInfo(file_info));
    EXPECT_EQ(file_info.type(), CARTA::FileType::ZARR);
    ASSERT_EQ(file_info.hdu_list_size(), 1);
    EXPECT_EQ(file_info.hdu_list(0), "SKY");
    const int64_t first = file_info.size();
    EXPECT_GT(first, 0);

    // The directory timestamp cannot see this growth, so an unchanged size is the kept one
    GrowAChunk(store, 4096);
    EXPECT_EQ(FileInfoSize(store), first);

    ForgetEverything();
    EXPECT_EQ(FileInfoSize(store), first + 4096);
}

TEST_F(ZarrStoresTest, AMalformedStoreIsNotMeasured) {
    EXPECT_EQ(FileInfoSize(AMalformedStore()), 0);
}
