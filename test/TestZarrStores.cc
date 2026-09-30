/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
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

#include "Cache/FileInfoCache.h"
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

    // A copy of the pixel fixture, which is an image store.
    fs::path AStore(const std::string& name = "store.zarr") {
        const auto store = _scratch / name;
        fs::copy(kZarrFixture, store, fs::copy_options::recursive);
        return store;
    }

    // An image store the XRADIO profile recognises and refuses: its root says it is an image dataset,
    // and leaves out the coordinate system every image needs.
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

    // A Zarr v3 group, and a good one, of something that is not an image.
    fs::path AZarrOfSomethingElse() {
        const auto store = _scratch / "other.zarr";
        fs::create_directories(store);
        WriteText(store / "zarr.json", R"({"zarr_format": 3, "node_type": "group", "attributes": {"type": "visibilities"}})");
        return store;
    }

    // A directory whose root metadata does not parse.
    fs::path AnUnparseableRoot() {
        const auto store = _scratch / "unparseable.zarr";
        fs::create_directories(store);
        WriteText(store / "zarr.json", "{ this is not json");
        return store;
    }

    static void ForgetEverything() {
        FileInfoCache::Instance().Clear();
        ZarrStores::Instance().Clear();
    }

    fs::path _scratch;
};

TEST_F(ZarrStoresTest, AStoreIsOpenedOnceAndOffersItsImages) {
    const auto look = ZarrStores::Instance().Look(AStore().string());
    EXPECT_EQ(look->kind, ZarrStoreLook::Kind::store);
    EXPECT_TRUE(look->IsZarr());
    EXPECT_TRUE(look->dataset.has_value());
    ASSERT_EQ(look->offered.size(), 1);
    EXPECT_EQ(look->offered.front(), "SKY");
    EXPECT_TRUE(look->reason.empty());
}

// ADR 0009 in carta-zarr: a store the profile recognised and found broken is not "no match". It is
// listed as the image it was meant to be, and everything that asks about it is told why it is broken.
TEST_F(ZarrStoresTest, AMalformedStoreIsAnImageThatSaysWhyItWillNotOpen) {
    const auto store = AMalformedStore().string();

    const auto look = ZarrStores::Instance().Look(store);
    EXPECT_EQ(look->kind, ZarrStoreLook::Kind::malformed);
    EXPECT_TRUE(look->IsZarr());
    EXPECT_FALSE(look->dataset.has_value());
    EXPECT_NE(look->reason.find("coordinate_system_info"), std::string::npos) << look->reason;

    // The file list shows it as an image.
    std::string message;
    EXPECT_EQ(FolderImageType(store, message), CARTA::FileType::ZARR);

    // Selecting it says why.
    CARTA::FileInfo file_info;
    FileInfoLoader loader(store);
    EXPECT_FALSE(loader.FillFileInfo(file_info));
    EXPECT_EQ(file_info.type(), CARTA::FileType::ZARR);
    EXPECT_NE(loader.Message().find(look->reason), std::string::npos) << loader.Message();

    // And so does opening it.
    try {
        CartaZarrImage image(store);
        FAIL() << "a malformed store opened";
    } catch (const casacore::AipsError& error) {
        EXPECT_NE(std::string(error.getMesg()).find(look->reason), std::string::npos) << error.getMesg();
    }
}

TEST_F(ZarrStoresTest, AZarrStoreOfSomethingElseIsNotAnImage) {
    const auto store = AZarrOfSomethingElse().string();
    const auto look = ZarrStores::Instance().Look(store);
    EXPECT_EQ(look->kind, ZarrStoreLook::Kind::none);
    EXPECT_FALSE(look->dataset.has_value());

    std::string message;
    EXPECT_NE(FolderImageType(store, message), CARTA::FileType::ZARR);
}

// Invalid metadata is also what the library calls a root it cannot parse, and a root it cannot parse
// was never recognised as an image.
TEST_F(ZarrStoresTest, AnUnparseableRootIsNotAnImage) {
    const auto store = AnUnparseableRoot().string();
    const auto look = ZarrStores::Instance().Look(store);
    EXPECT_EQ(look->kind, ZarrStoreLook::Kind::none);

    std::string message;
    EXPECT_NE(FolderImageType(store, message), CARTA::FileType::ZARR);
}

TEST_F(ZarrStoresTest, AFileIsNotAStore) {
    const auto file = _scratch / "zarr.json";
    fs::copy_file(kZarrFixture / "zarr.json", file);
    EXPECT_EQ(ZarrStores::Instance().Look(file.string())->kind, ZarrStoreLook::Kind::none);
    EXPECT_EQ(ZarrStores::Instance().Look((_scratch / "missing.zarr").string())->kind, ZarrStoreLook::Kind::none);
}

TEST_F(ZarrStoresTest, ALookIsKeptAndSharedByEveryoneWhoAsks) {
    ZarrStores stores(4, std::chrono::seconds(30));
    const auto store = AStore().string();

    const auto first = stores.Look(store);
    EXPECT_EQ(stores.Look(store), first);
    // The same path however it is spelled.
    EXPECT_EQ(stores.Look(store + "/"), first);

    stores.Clear();
    EXPECT_NE(stores.Look(store), first);
}

// A directory's own timestamp moves when an entry is added to it or taken from it, and a store whose
// root has changed is looked at again.
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
