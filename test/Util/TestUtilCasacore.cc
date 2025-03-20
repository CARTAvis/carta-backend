/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "Util/Casacore.h"

#include "CommonTestUtilities.h"

class CasacoreUtilTest : public ::testing::Test {};

TEST(CasacoreUtilTest, SubdirectoryAbs) {
    auto pwd = TestRoot();
    EXPECT_TRUE(IsSubdirectory((pwd / "data").string(), pwd.string()));
    EXPECT_FALSE(IsSubdirectory(pwd.string(), (pwd / "data").string()));
    EXPECT_TRUE(IsSubdirectory((pwd / "data/images").string(), pwd.string()));
    EXPECT_FALSE(IsSubdirectory(pwd.string(), (pwd / "data/images").string()));
    EXPECT_TRUE(IsSubdirectory((pwd / "data/images").string(), (pwd / "data").string()));
    EXPECT_FALSE(IsSubdirectory((pwd / "data").string(), (pwd / "data/images").string()));
    EXPECT_TRUE(IsSubdirectory((pwd / "data/images/fits").string(), (pwd / "data/images").string()));
    EXPECT_FALSE(IsSubdirectory((pwd / "data/images/fits").string(), (pwd / "data/images/hdf5").string()));
}

TEST(CasacoreUtilTest, SubdirectoryRel) {
    fs::current_path(TestRoot());
    EXPECT_TRUE(IsSubdirectory("./data", "./"));
    EXPECT_FALSE(IsSubdirectory("./", "./data"));
    EXPECT_TRUE(IsSubdirectory("./data/images", "./"));
    EXPECT_FALSE(IsSubdirectory("./", "./data/images"));
    EXPECT_TRUE(IsSubdirectory("./data/images", "./data"));
    EXPECT_FALSE(IsSubdirectory("./data", "./data/images"));
    EXPECT_TRUE(IsSubdirectory("./data/images/fits", "./data/images"));
    EXPECT_FALSE(IsSubdirectory("./data/images/fits", "./data/images/hdf5"));
}

TEST(CasacoreUtilTest, SubdirectorySelf) {
    auto pwd = TestRoot();
    EXPECT_TRUE(IsSubdirectory("/", "/"));
    EXPECT_TRUE(IsSubdirectory("./", "./"));
    EXPECT_TRUE(IsSubdirectory(pwd.string(), pwd.string()));
    EXPECT_TRUE(IsSubdirectory((pwd / ".").string(), pwd.string()));
    EXPECT_TRUE(IsSubdirectory(pwd.string(), (pwd / ".").string()));
}

TEST(CasacoreUtilTest, ParentNotSubdirectory) {
    auto pwd = TestRoot();
    EXPECT_FALSE(IsSubdirectory(pwd.parent_path().string(), pwd.string()));
    EXPECT_FALSE(IsSubdirectory((pwd / "..").string(), pwd.string()));
    EXPECT_FALSE(IsSubdirectory("../", "./"));
}

TEST(CasacoreUtilTest, TopIsRoot) {
    auto pwd = TestRoot();
    EXPECT_TRUE(IsSubdirectory(pwd.string(), "/"));
    EXPECT_TRUE(IsSubdirectory("./", "/"));
}
