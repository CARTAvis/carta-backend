/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>
#include "Util/File.h"

#include "CommonTestUtilities.h"

class FileUtilTest : public ::testing::Test {
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

TEST_F(FileUtilTest, ItemCountValidFolder) {
    auto pwd = TestRoot();
    EXPECT_EQ(GetNumItems((pwd / "data/tables").string()), 2);
    EXPECT_EQ(GetNumItems((pwd / "data/tables/xml").string()), 6);
}

TEST_F(FileUtilTest, ItemCountMissingFolder) {
    auto pwd = TestRoot();
    EXPECT_EQ(GetNumItems((pwd / "data/missing_folder").string()), -1);
}

TEST(CheckFolderPathsTest, ValidPaths) {
    auto pwd = TestRoot();
    std::string top_level = (pwd / "data").string();
    std::string starting = (pwd / "data" / "test").string();

    fs::create_directories(starting);

    EXPECT_TRUE(CheckFolderPaths(top_level, starting));

    fs::remove(starting);
}

TEST(CheckFolderPathsTest, NonExistentStartingDirectory) {
    auto pwd = TestRoot();
    std::string top_level = (pwd / "data").string();
    std::string starting = (pwd / "data" / "test").string();

    EXPECT_TRUE(CheckFolderPaths(top_level, starting));
}

TEST(CheckFolderPathsTest, InvalidTopLevelDirectory) {
    std::string top_level = "/data/nonexistent";
    auto pwd = TestRoot();
    std::string starting = (pwd / "data" / "test").string();

    fs::create_directories(starting);

    EXPECT_FALSE(CheckFolderPaths(top_level, starting));

    fs::remove(starting);
}

TEST(CheckFolderPathsTest, TopLevelBase) {
    std::string top_level = "base";
    auto pwd = TestRoot();
    std::string starting = (pwd / "data" / "test").string();

    fs::create_directories(starting);

    EXPECT_TRUE(CheckFolderPaths(top_level, starting));
    EXPECT_EQ(top_level, starting);

    fs::remove(starting);
}

TEST(CheckFolderPathsTest, StartingRoot) {
    auto pwd = TestRoot();
    std::string top_level = pwd;
    std::string starting = "root";

    EXPECT_TRUE(CheckFolderPaths(top_level, starting));
    EXPECT_EQ(starting, top_level);
}

TEST(CheckFolderPathsTest, StartingNotSubdirectory) {
    auto pwd = TestRoot();
    std::string top_level = (pwd / "data" / "parent").string();
    std::string starting = (pwd / "data" / "another").string();

    fs::create_directories(top_level);
    fs::create_directories(starting);

    EXPECT_FALSE(CheckFolderPaths(top_level, starting));

    fs::remove_all(top_level);
    fs::remove_all(starting);
}

TEST(CheckFolderPathsTest, SamePath) {
    auto pwd = TestRoot();
    std::string top_level = (pwd / "data" / "test_same").string();
    std::string starting = (pwd / "data" / "test_same").string();

    fs::create_directories(top_level);

    EXPECT_TRUE(CheckFolderPaths(top_level, starting));

    fs::remove(top_level);
}
