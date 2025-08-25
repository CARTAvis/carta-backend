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
    EXPECT_EQ(GetNumItems((pwd / "carta-backend-test-data/tables").string()), 2);
    EXPECT_EQ(GetNumItems((pwd / "carta-backend-test-data/tables/xml").string()), 6);
}

TEST_F(FileUtilTest, ItemCountMissingFolder) {
    auto pwd = TestRoot();
    EXPECT_EQ(GetNumItems((pwd / "carta-backend-test-data/missing_folder").string()), -1);
}

TEST(IsSubdirectoryTest, ValidSubdirectory) {
    EXPECT_TRUE(IsSubdirectory("/home/user/docs", "/home/user"));
    EXPECT_TRUE(IsSubdirectory("/var/log/apache", "/var/log"));
    EXPECT_TRUE(IsSubdirectory("/usr/local/bin", "/usr/local"));

    auto pwd = TestRoot();
    std::string top_level = (pwd / "carta-backend-test-data").string();
    std::string starting = (pwd / "carta-backend-test-data" / "test").string();

    fs::create_directories(starting);

    EXPECT_TRUE(IsSubdirectory(starting, top_level));

    fs::remove(starting);
}

TEST(IsSubdirectoryTest, SameDirectory) {
    EXPECT_TRUE(IsSubdirectory("/home/user", "/home/user"));
    EXPECT_TRUE(IsSubdirectory("/etc", "/etc"));
}

TEST(IsSubdirectoryTest, ParentDirectory) {
    EXPECT_FALSE(IsSubdirectory("/home", "/home/user"));
    EXPECT_FALSE(IsSubdirectory("/var", "/var/log"));
}

TEST(IsSubdirectoryTest, InvalidPaths) {
    EXPECT_FALSE(IsSubdirectory("/this/does/not/exist", "/home/user"));
    EXPECT_FALSE(IsSubdirectory("/home/user", "/this/does/not/exist"));
}

TEST(IsSubdirectoryTest, SubdirectoryAbs) {
    auto pwd = TestRoot();
    EXPECT_TRUE(IsSubdirectory((pwd / "carta-backend-test-data").string(), pwd.string()));
    EXPECT_FALSE(IsSubdirectory(pwd.string(), (pwd / "carta-backend-test-data").string()));
    EXPECT_TRUE(IsSubdirectory((pwd / "carta-backend-test-data/images").string(), pwd.string()));
    EXPECT_FALSE(IsSubdirectory(pwd.string(), (pwd / "carta-backend-test-data/images").string()));
    EXPECT_TRUE(IsSubdirectory((pwd / "carta-backend-test-data/images").string(), (pwd / "carta-backend-test-data").string()));
    EXPECT_FALSE(IsSubdirectory((pwd / "carta-backend-test-data").string(), (pwd / "carta-backend-test-data/images").string()));
    EXPECT_TRUE(IsSubdirectory((pwd / "carta-backend-test-data/images/fits").string(), (pwd / "carta-backend-test-data/images").string()));
    EXPECT_FALSE(IsSubdirectory((pwd / "carta-backend-test-data/images/fits").string(), (pwd / "carta-backend-test-data/images/hdf5").string()));
}

TEST(IsSubdirectoryTest, SubdirectoryRel) {
    fs::current_path(TestRoot());
    EXPECT_TRUE(IsSubdirectory("./carta-backend-test-data", "./"));
    EXPECT_FALSE(IsSubdirectory("./", "./carta-backend-test-data"));
    EXPECT_TRUE(IsSubdirectory("./carta-backend-test-data/images", "./"));
    EXPECT_FALSE(IsSubdirectory("./", "./carta-backend-test-data/images"));
    EXPECT_TRUE(IsSubdirectory("./carta-backend-test-data/images", "./carta-backend-test-data"));
    EXPECT_FALSE(IsSubdirectory("./carta-backend-test-data", "./data/carta-backend-test-data"));
    EXPECT_TRUE(IsSubdirectory("./carta-backend-test-data/images/fits", "./carta-backend-test-data/images"));
    EXPECT_FALSE(IsSubdirectory("./carta-backend-test-data/images/fits", "./carta-backend-test-data/images/hdf5"));
}

TEST(IsSubdirectoryTest, SubdirectorySelf) {
    auto pwd = TestRoot();
    EXPECT_TRUE(IsSubdirectory("/", "/"));
    EXPECT_TRUE(IsSubdirectory("./", "./"));
    EXPECT_TRUE(IsSubdirectory(pwd.string(), pwd.string()));
    EXPECT_TRUE(IsSubdirectory((pwd / ".").string(), pwd.string()));
    EXPECT_TRUE(IsSubdirectory(pwd.string(), (pwd / ".").string()));
}

TEST(IsSubdirectoryTest, ParentNotSubdirectory) {
    auto pwd = TestRoot();
    EXPECT_FALSE(IsSubdirectory(pwd.parent_path().string(), pwd.string()));
    EXPECT_FALSE(IsSubdirectory((pwd / "..").string(), pwd.string()));
    EXPECT_FALSE(IsSubdirectory("../", "./"));
}

TEST(IsSubdirectoryTest, TopIsRoot) {
    auto pwd = TestRoot();
    EXPECT_TRUE(IsSubdirectory(pwd.string(), "/"));
    EXPECT_TRUE(IsSubdirectory("./", "/"));
}

TEST(IsSubdirectoryTest, SubdirectoryCheck) {
    EXPECT_TRUE(IsSubdirectory("/carta-backend-test-data/test", "/carta-backend-test-data"));
    EXPECT_FALSE(IsSubdirectory("/etc", "/top"));
    EXPECT_TRUE(IsSubdirectory("/carta-backend-test-data/test/sub", "/carta-backend-test-data/test"));
}

TEST(CheckFolderPathsTest, ValidPaths) {
    auto pwd = TestRoot();
    std::string top_level = (pwd / "carta-backend-test-data").string();
    std::string starting = (pwd / "carta-backend-test-data" / "test").string();

    fs::create_directories(starting);

    EXPECT_TRUE(CheckFolderPaths(top_level, starting));

    fs::remove(starting);
}

TEST(CheckFolderPathsTest, NonExistentStartingDirectory) {
    auto pwd = TestRoot();
    std::string top_level = (pwd / "carta-backend-test-data").string();
    std::string starting = (pwd / "carta-backend-test-data" / "test").string();

    EXPECT_TRUE(CheckFolderPaths(top_level, starting));
}

TEST(CheckFolderPathsTest, InvalidTopLevelDirectory) {
    std::string top_level = "/carta-backend-test-data/nonexistent";
    auto pwd = TestRoot();
    std::string starting = (pwd / "carta-backend-test-data" / "test").string();

    fs::create_directories(starting);

    EXPECT_FALSE(CheckFolderPaths(top_level, starting));

    fs::remove(starting);
}

TEST(CheckFolderPathsTest, TopLevelBase) {
    std::string top_level = "base";
    auto pwd = TestRoot();
    std::string starting = (pwd / "carta-backend-test-data" / "test").string();

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
    std::string top_level = (pwd / "carta-backend-test-data" / "parent").string();
    std::string starting = (pwd / "carta-backend-test-data" / "another").string();

    fs::create_directories(top_level);
    fs::create_directories(starting);

    EXPECT_FALSE(CheckFolderPaths(top_level, starting));

    fs::remove_all(top_level);
    fs::remove_all(starting);
}

TEST(CheckFolderPathsTest, SamePath) {
    auto pwd = TestRoot();
    std::string top_level = (pwd / "carta-backend-test-data" / "test_same").string();
    std::string starting = (pwd / "carta-backend-test-data" / "test_same").string();

    fs::create_directories(top_level);

    EXPECT_TRUE(CheckFolderPaths(top_level, starting));

    fs::remove(top_level);
}
