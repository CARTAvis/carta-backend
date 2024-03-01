/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2024 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "Util/Casacore.h"
#include "Util/File.h"
#include "Util/String.h"

#include "CommonTestUtilities.h"

class UtilTest : public ::testing::Test {
public:
    void SetUp() {
        working_directory = fs::current_path();
    }

    void TearDown() {
        fs::current_path(working_directory);
    }

private:
    fs::path working_directory;
};

TEST(UtilTest, SubdirectoryAbs) {
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

TEST(UtilTest, SubdirectoryRel) {
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

TEST(UtilTest, SubdirectorySelf) {
    auto pwd = TestRoot();
    EXPECT_TRUE(IsSubdirectory("/", "/"));
    EXPECT_TRUE(IsSubdirectory("./", "./"));
    EXPECT_TRUE(IsSubdirectory(pwd.string(), pwd.string()));
    EXPECT_TRUE(IsSubdirectory((pwd / ".").string(), pwd.string()));
    EXPECT_TRUE(IsSubdirectory(pwd.string(), (pwd / ".").string()));
}

TEST(UtilTest, ParentNotSubdirectory) {
    auto pwd = TestRoot();
    EXPECT_FALSE(IsSubdirectory(pwd.parent_path().string(), pwd.string()));
    EXPECT_FALSE(IsSubdirectory((pwd / "..").string(), pwd.string()));
    EXPECT_FALSE(IsSubdirectory("../", "./"));
}

TEST(UtilTest, TopIsRoot) {
    auto pwd = TestRoot();
    EXPECT_TRUE(IsSubdirectory(pwd.string(), "/"));
    EXPECT_TRUE(IsSubdirectory("./", "/"));
}

TEST(UtilTest, ItemCountValidFolder) {
    auto pwd = TestRoot();
    EXPECT_EQ(GetNumItems((pwd / "data/tables").string()), 2);
    EXPECT_EQ(GetNumItems((pwd / "data/tables/xml").string()), 6);
}

TEST(UtilTest, ItemCountMissingFolder) {
    auto pwd = TestRoot();
    EXPECT_EQ(GetNumItems((pwd / "data/missing_folder").string()), -1);
}

TEST(UtilTest, StringCompare) {
    EXPECT_TRUE(ConstantTimeStringCompare("hello world", "hello world"));
    EXPECT_FALSE(ConstantTimeStringCompare("hello w1rld", "hello world"));
    EXPECT_FALSE(ConstantTimeStringCompare("hello w1rld", "hello w2rld"));
    EXPECT_FALSE(ConstantTimeStringCompare("hello w", "hello world"));
    EXPECT_TRUE(ConstantTimeStringCompare("", ""));
    EXPECT_FALSE(ConstantTimeStringCompare("hello world", ""));
    EXPECT_FALSE(ConstantTimeStringCompare("", "hello world"));
}

TEST(UtilTest, HasSuffixCaseSensitive) {
    EXPECT_TRUE(HasSuffix("test.fits", ".fits", true));
    EXPECT_FALSE(HasSuffix("test.FITS", ".fits", true));
    EXPECT_FALSE(HasSuffix("test.fits", ".FITS", true));
    EXPECT_FALSE(HasSuffix("test.fits", ".xml", true));
    EXPECT_TRUE(HasSuffix("test.fits.gz", ".fits.gz", true));
    EXPECT_FALSE(HasSuffix("test.fits.gz", ".fits", true));
}

TEST(UtilTest, HasSuffixCaseInsensitive) {
    EXPECT_TRUE(HasSuffix("test.fits", ".fits"));
    EXPECT_TRUE(HasSuffix("test.FITS", ".fits"));
    EXPECT_TRUE(HasSuffix("test.fits", ".FITS"));
    EXPECT_FALSE(HasSuffix("test.fits", ".xml"));
    EXPECT_TRUE(HasSuffix("test.fits.gz", ".fits.gz"));
    EXPECT_FALSE(HasSuffix("test.fits.gz", ".fits"));
}
