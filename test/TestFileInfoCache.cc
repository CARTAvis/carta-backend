/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "Cache/FileInfoCache.h"
#include "FileList/FileInfoLoader.h"
#include "ImageData/ZarrStores.h"

using namespace carta;

namespace fs = std::filesystem;

namespace {

const fs::path kZarrFixture{ZARR_PIXEL_FIXTURE};

FileInfoCache::Entry SomeEntry(int64_t size) {
    FileInfoCache::Entry entry;
    entry.size = size;
    entry.hdu_list.emplace_back("SKY");
    entry.complete = true;
    return entry;
}

FileInfoCache::Stamp SomeStamp(int64_t mtime) {
    FileInfoCache::Stamp stamp;
    stamp.directory_mtime = mtime;
    return stamp;
}

// A chunk file several levels below the root, which is where a Zarr store keeps its data and
// where a write is invisible to every timestamp above it.
fs::path SomeChunk(const fs::path& root) {
    for (const auto& entry : fs::recursive_directory_iterator(root / "SKY" / "c")) {
        if (entry.is_regular_file()) {
            return entry.path();
        }
    }
    return {};
}

} // namespace

class FileInfoCacheTest : public ::testing::Test {
public:
    void SetUp() override {
        if (!fs::exists(kZarrFixture)) {
            GTEST_SKIP() << "carta-zarr pixel fixture not found at " << kZarrFixture;
        }
        _store = fs::temp_directory_path() / ("carta_file_info_cache_" + std::to_string(::getpid()));
        fs::remove_all(_store);
        fs::copy(kZarrFixture, _store, fs::copy_options::recursive);
        ForgetEverything();
    }

    void TearDown() override {
        ForgetEverything();
        std::error_code error;
        fs::remove_all(_store, error);
    }

    int64_t FillSize(bool* complete = nullptr) {
        CARTA::FileInfo file_info;
        FileInfoLoader loader(_store.string(), CARTA::FileType::ZARR);
        const bool ok = loader.FillFileInfo(file_info);
        if (complete) {
            *complete = ok;
        }
        return file_info.size();
    }

    // The way Session::FillFileInfo arrives: no type in hand, so the loader has to work it out.
    CARTA::FileInfo FillWithoutType() {
        CARTA::FileInfo file_info;
        FileInfoLoader loader(_store.string());
        loader.FillFileInfo(file_info);
        return file_info;
    }

    // Append to a chunk so the store really is bigger, without touching any directory's entry list.
    void GrowAChunk(std::size_t bytes) {
        const auto chunk = SomeChunk(_store);
        ASSERT_FALSE(chunk.empty());
        std::ofstream stream(chunk, std::ios::binary | std::ios::app);
        stream << std::string(bytes, 'x');
    }

    // Both answers about a directory image: what the file info derived, and what the store was.
    static void ForgetEverything() {
        FileInfoCache::Instance().Clear();
        ZarrStores::Instance().Clear();
    }

    fs::path _store;
};

// The repetition the cache exists for: the folder listing, the selection and the open all derive
// the same image, and only the first of them pays for it.
// A file type is sent as its number, so a number once given keeps its meaning: a frontend or script
// built before Zarr was added reads 6 as UNKNOWN, and must go on doing so. ZARR was first given 6,
// with UNKNOWN moved to 7.
TEST(FileTypeNumbers, ANumberKeepsTheTypeItWasGiven) {
    EXPECT_EQ(static_cast<int>(CARTA::FileType::MIRIAD), 5);
    EXPECT_EQ(static_cast<int>(CARTA::FileType::UNKNOWN), 6);
    EXPECT_EQ(static_cast<int>(CARTA::FileType::ZARR), 7);
}

TEST_F(FileInfoCacheTest, RepeatedFillIsServedFromTheCache) {
    bool complete = false;
    const int64_t first = FillSize(&complete);
    EXPECT_TRUE(complete);
    EXPECT_GT(first, 0);

    // The store grows by an amount no cheap stamp can see, so an unchanged answer can only have
    // come from the cache.
    GrowAChunk(4096);
    EXPECT_EQ(FillSize(), first);

    FileInfoCache::Instance().Clear();
    EXPECT_EQ(FillSize(), first + 4096);
}

// A caller that arrives without a type used to probe the store just to name it, once when the file
// was selected and again when it was opened. The type is decided by the same probe as everything
// else here, so a hit answers it too and the second caller probes nothing.
TEST_F(FileInfoCacheTest, TheTypeIsAnsweredFromTheCacheToo) {
    const auto first = FillWithoutType();
    EXPECT_EQ(first.type(), CARTA::FileType::ZARR);
    ASSERT_EQ(first.hdu_list_size(), 1);

    // Break the store so that any fresh probe would have to call it something other than Zarr,
    // while leaving every timestamp the stamp reads untouched.
    const auto metadata = _store / "zarr.json";
    const auto mtime = fs::last_write_time(metadata);
    const auto size = fs::file_size(metadata);
    {
        std::ofstream stream(metadata, std::ios::binary | std::ios::trunc);
    }
    fs::resize_file(metadata, size);
    fs::last_write_time(metadata, mtime);

    const auto second = FillWithoutType();
    EXPECT_EQ(second.type(), CARTA::FileType::ZARR);
    EXPECT_EQ(second.size(), first.size());
    EXPECT_EQ(second.hdu_list_size(), first.hdu_list_size());

    // And once the caches are out of the way, the damage shows -- proving the store really was
    // unreadable for the whole of the check above.
    ForgetEverything();
    EXPECT_NE(FillWithoutType().type(), CARTA::FileType::ZARR);
}

// Rewriting a store's root metadata in place is invisible to the directory's own timestamp, and the
// cache does not read the metadata to find out: where a store keeps it is the library's business. The
// time a answer is kept for is what bounds it, as it bounds every other write this cannot see.
TEST_F(FileInfoCacheTest, RootMetadataRewriteWaitsForTheTtl) {
    const int64_t first = FillSize();
    GrowAChunk(4096);

    const auto metadata = _store / "zarr.json";
    const auto before = fs::last_write_time(metadata);
    fs::last_write_time(metadata, before + std::chrono::seconds(2));
    EXPECT_EQ(FillSize(), first);

    ForgetEverything();
    EXPECT_EQ(FillSize(), first + 4096);
}

// A directory's own timestamp does move when an entry is added to it.
TEST_F(FileInfoCacheTest, NewRootEntryIsNoticed) {
    const int64_t first = FillSize();
    GrowAChunk(4096);

    fs::create_directory(_store / "NEW_VARIABLE");

    EXPECT_EQ(FillSize(), first + 4096);
}

TEST(FileInfoCacheContainerTest, HitWithinTheTtl) {
    FileInfoCache cache(4, std::chrono::seconds(30));
    cache.Put("a", SomeStamp(1), SomeEntry(10));

    auto entry = cache.Get("a", SomeStamp(1));
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->size, 10);
    EXPECT_TRUE(entry->complete);
    ASSERT_EQ(entry->hdu_list.size(), 1);
    EXPECT_EQ(entry->hdu_list[0], "SKY");
}

TEST(FileInfoCacheContainerTest, MissOnceTheTtlHasPassed) {
    FileInfoCache cache(4, std::chrono::seconds(0));
    cache.Put("a", SomeStamp(1), SomeEntry(10));
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    EXPECT_FALSE(cache.Get("a", SomeStamp(1)).has_value());
}

TEST(FileInfoCacheContainerTest, MissOnAChangedStamp) {
    FileInfoCache cache(4, std::chrono::seconds(30));
    cache.Put("a", SomeStamp(1), SomeEntry(10));

    EXPECT_FALSE(cache.Get("a", SomeStamp(2)).has_value());
    // The entry that did not match is gone rather than shadowing the next one.
    cache.Put("a", SomeStamp(2), SomeEntry(20));
    auto entry = cache.Get("a", SomeStamp(2));
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->size, 20);
}

TEST(FileInfoCacheContainerTest, TheLeastRecentlyUsedEntryIsEvicted) {
    FileInfoCache cache(2, std::chrono::seconds(30));
    cache.Put("a", SomeStamp(1), SomeEntry(10));
    cache.Put("b", SomeStamp(1), SomeEntry(20));

    // Using "a" makes "b" the oldest.
    ASSERT_TRUE(cache.Get("a", SomeStamp(1)).has_value());
    cache.Put("c", SomeStamp(1), SomeEntry(30));

    EXPECT_TRUE(cache.Get("a", SomeStamp(1)).has_value());
    EXPECT_FALSE(cache.Get("b", SomeStamp(1)).has_value());
    EXPECT_TRUE(cache.Get("c", SomeStamp(1)).has_value());
}
