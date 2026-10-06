/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "ImageData/ZarrContext.h"

using carta::ZarrContextOptions;

TEST(ZarrContextTest, FileIoThreadsArePassedOnWhenPositive) {
    EXPECT_EQ(ZarrContextOptions(3, -1, 1024, 8).io_threads, 3U);
}

TEST(ZarrContextTest, FileIoThreadsOfZeroOrLessLeaveTheLibraryDefault) {
    EXPECT_EQ(ZarrContextOptions(0, -1, 1024, 8).io_threads, 0U);
    EXPECT_EQ(ZarrContextOptions(-4, -1, 1024, 8).io_threads, 0U);
}

TEST(ZarrContextTest, DataCopyThreadsArePassedOnWhenPositive) {
    EXPECT_EQ(ZarrContextOptions(2, 7, 1024, 8).decode_threads, 7U);
}

TEST(ZarrContextTest, DataCopyThreadsOfZeroOrLessFollowTheOpenMpThreads) {
    EXPECT_EQ(ZarrContextOptions(2, -1, 1024, 8).decode_threads, 8U);
    EXPECT_EQ(ZarrContextOptions(2, 0, 1024, 8).decode_threads, 8U);
    EXPECT_EQ(ZarrContextOptions(2, -1, 1024, 0).decode_threads, 1U);
}

TEST(ZarrContextTest, CacheSizeIsGivenInMebibytes) {
    const auto options = ZarrContextOptions(2, -1, 256, 8);
    ASSERT_TRUE(options.cache_bytes.has_value());
    EXPECT_EQ(*options.cache_bytes, 256U * 1024U * 1024U);
}

TEST(ZarrContextTest, CacheSizeOfZeroDisablesTheCache) {
    const auto options = ZarrContextOptions(2, -1, 0, 8);
    ASSERT_TRUE(options.cache_bytes.has_value());
    EXPECT_EQ(*options.cache_bytes, 0U);
}

TEST(ZarrContextTest, ConfigureThenGetDoesNotThrow) {
    EXPECT_NO_THROW(carta::ConfigureZarrContext(2, 1, 0, 1));
    EXPECT_NO_THROW(carta::GetZarrContext());
}
