/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

#include "Util/Casacore.h"

#include "CommonTestUtilities.h"

void CreateFile(const fs::path& path, const std::string& content = "") {
    std::ofstream file(path);
    file << content;
    file.close();
}

class GetResolvedFilenameTest : public ::testing::Test {
protected:
    fs::path temp_dir;

    void SetUp() override {
        temp_dir = fs::temp_directory_path() / "test_symlink";
        fs::create_directories(temp_dir);
    }

    void TearDown() override {
        fs::remove_all(temp_dir);
    }
};

// PREVIOUSLY FAILING
// this test seems to be working fine when I run it on my local machine
TEST_F(GetResolvedFilenameTest, ResolvesNormalFile) {
	fs::path dir = temp_dir / "subdir";
	fs::create_directory(dir);
	fs::path file = dir / "test.txt";
	CreateFile(file, "sample content");
	std::string message;
	std::string result = GetResolvedFilename(temp_dir.string(), "subdir", "test.txt", message);
	EXPECT_EQ(result, file.string());
	EXPECT_TRUE(message.empty());
}

/* FAILING
* expected output -> /tmp/test_symlink/subdir/symlink.txt
* actual output -> /tmp/test_symlink/subdir/real.txt
* the GetResolvedFilename function resolves the filename to the real file instead
* where it actualy should resolve to the file through the symlink
*
* path.resolvedName() -> it is a known casacore issue that it fails to resolve symlinks
* path.expandedName() -> if resolvedName() fails it tries expandedName() which returns the expanded name but neccessarily the resolved path
*/

TEST_F(GetResolvedFilenameTest, ResolvesSymlink) {
	fs::path dir = temp_dir / "subdir";
	fs::create_directory(dir);
	fs::path target_file = dir / "real.txt";
	CreateFile(target_file, "actual file");
	
	fs::path symlink_file = dir / "symlink.txt";
	fs::create_symlink(target_file, symlink_file);
	
	std::string message;
	std::string result = GetResolvedFilename(temp_dir.string(), "subdir", "symlink.txt", message);
	
	EXPECT_EQ(result, symlink_file.string());
	EXPECT_TRUE(message.empty());
}

/** FAILING
* expected output -> ""
* actual output -> /tmp/test_symlink/real_dir
* It fails becuase it resolves to a real directory instead of an empty string
* it depends on if the function is intended to be used in this manner, but again it is a problem with casacore's resolvedName() function not handling symlinks correctly
*/

TEST_F(GetResolvedFilenameTest, DirectorySymlink) {
	fs::path real_dir = temp_dir / "real_dir";
	fs::create_directory(real_dir);
	
	fs::path symlink_dir = temp_dir / "symlink_dir";
	fs::create_symlink(real_dir, symlink_dir);
	
	std::string message;
	std::string result = GetResolvedFilename(temp_dir.string(), "", "symlink_dir", message);
	
	EXPECT_EQ(result, ""); // It should not resolve a directory symlink as a file
	EXPECT_FALSE(message.empty());
}