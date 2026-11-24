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
        temp_dir = TestRoot() / "test" / "test_symlink";
        fs::create_directories(temp_dir);
    }

    void TearDown() override {
        fs::remove_all(temp_dir);
    }
};

TEST_F(GetResolvedFilenameTest, SymlinkToNonExistentFile) {
    fs::path dir = temp_dir / "subdir";
    fs::create_directory(dir);

    fs::path target_file = dir / "missing.txt";
    fs::path symlink_file = dir / "bad_symlink.txt";
    fs::create_symlink(target_file, symlink_file); // Points to a missing file

    std::string message;
    std::string result = GetResolvedFilename(temp_dir.string(), "subdir", "bad_symlink.txt", message);

    EXPECT_EQ(result, ""); // Should return an empty string
    EXPECT_FALSE(message.empty());
}

TEST_F(GetResolvedFilenameTest, FileExists) {
    auto pwd = TestRoot();
    std::string message;
    std::string resolved = GetResolvedFilename(pwd, "data/images/fits", "noise_4d.fits", message);

    EXPECT_FALSE(resolved.empty());
    EXPECT_TRUE(message.empty());
}

TEST_F(GetResolvedFilenameTest, FileDoesNotExist) {
    std::string message;
    std::string resolved = GetResolvedFilename("/", "test_dir", "missing.txt", message);

    EXPECT_TRUE(resolved.empty());
    EXPECT_FALSE(message.empty());
}

TEST_F(GetResolvedFilenameTest, MessageUsesRequestedPath) {
    setenv("CARTA_TEST_VARIABLE", "expanded-value", 1);
    std::string dir_message, file_message;
    GetResolvedFilename(temp_dir.string(), "$CARTA_TEST_VARIABLE", "missing.txt", dir_message);
    GetResolvedFilename(temp_dir.string(), "", "$CARTA_TEST_VARIABLE", file_message);
    unsetenv("CARTA_TEST_VARIABLE");

    EXPECT_FALSE(dir_message.empty());
    EXPECT_EQ(dir_message.find("expanded-value"), std::string::npos);
    EXPECT_FALSE(file_message.empty());
    EXPECT_EQ(file_message.find("expanded-value"), std::string::npos);
}

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

TEST_F(GetResolvedFilenameTest, ResolvesToSameDirectory) {
    fs::path dir = temp_dir / "subdir";
    fs::create_directory(dir);
    std::string message;
    std::string result1 = GetResolvedFilename(temp_dir.string(), "", "subdir", message);
    std::string result2 = GetResolvedFilename(temp_dir.string(), "subdir", "", message);
    EXPECT_EQ(result1, dir.string());
    EXPECT_EQ(result2, dir.string());
    EXPECT_TRUE(message.empty());
}

TEST_F(GetResolvedFilenameTest, ResolvesSymlink) {
    fs::path dir = temp_dir / "subdir";
    fs::create_directory(dir);
    fs::path target_file = dir / "real.txt";
    CreateFile(target_file, "actual file");

    fs::path symlink_file = dir / "symlink.txt";
    fs::create_symlink(target_file, symlink_file);

    std::string message;
    std::string result = GetResolvedFilename(temp_dir.string(), "subdir", "symlink.txt", message);

    EXPECT_EQ(result, target_file.string());
    EXPECT_TRUE(message.empty());
}

TEST_F(GetResolvedFilenameTest, DirectorySymlink) {
    fs::path real_dir = temp_dir / "real_dir";
    fs::create_directory(real_dir);

    fs::path symlink_dir = temp_dir / "symlink_dir";
    fs::create_symlink(real_dir, symlink_dir);

    std::string message;
    std::string result = GetResolvedFilename(temp_dir.string(), "", "symlink_dir", message);

    EXPECT_EQ(result, real_dir);
    EXPECT_TRUE(message.empty());
}

TEST_F(GetResolvedFilenameTest, RefusesFileOutsideRoot) {
    fs::create_directory(temp_dir / "subdir");
    CreateFile(temp_dir / "outside.txt");

    std::string message;
    std::string result = GetResolvedFilename((temp_dir / "subdir").string(), "", "../outside.txt", message);

    EXPECT_EQ(result, "");
    EXPECT_EQ(message, "Forbidden path.");
}

TEST(GetPathInFolderTest, ChecksExpandedPath) {
    setenv("CARTA_TEST_PARENT", "..", 1);
    EXPECT_EQ(GetPathInFolder("/top/$CARTA_TEST_PARENT/x", "/top"), "");
    EXPECT_EQ(GetPathInFolder("/top/$CARTA_TEST_PARENT/x", "/top", false), "/top/$CARTA_TEST_PARENT/x");
    unsetenv("CARTA_TEST_PARENT");
    EXPECT_EQ(GetPathInFolder("/top2/x", "/top"), "");
    EXPECT_EQ(GetPathInFolder("/top/a/../x", "/top"), "/top/x");
    EXPECT_EQ(GetPathInFolder("/top/a/..", "/top/"), "/top");
}

class CheckExpressionNamesTest : public ::testing::Test {
protected:
    fs::path root, top, outside;

    void SetUp() override {
        root = fs::weakly_canonical(TestRoot()) / "test" / "test_expression_names";
        top = root / "top";
        outside = root / "outside";
        fs::remove_all(root);
        fs::create_directories(top / "sub" / "deep");
        fs::create_directories(top / "b:c");
        fs::create_directories(outside / "run1");
        fs::create_directory_symlink(outside, top / "link");
        fs::create_directory_symlink("sub/deep", top / "latest");
        fs::create_directory_symlink(top, root / "alias");
        fs::create_symlink("loop", top / "loop");
    }

    void TearDown() override {
        fs::remove_all(root);
    }

    // Any exception other than the refusal stops the backend, so it is left to fail the test
    bool Allowed(const std::string& expression, const fs::path& directory) {
        try {
            CheckExpressionNames(expression, directory.string(), top.string());
        } catch (const casacore::AipsError&) {
            return false;
        }
        return true;
    }
};

TEST_F(CheckExpressionNamesTest, AllowsNamesInsideFolder) {
    EXPECT_TRUE(Allowed("\"a.fits\" + b.fits * 2.5e3", top));
    EXPECT_TRUE(Allowed("\"sub/a.fits\" + sub\\/a.fits", top));
    EXPECT_TRUE(Allowed("\"../a.fits\" + \"" + (top / "a.fits").string() + "\"", top / "sub"));
    EXPECT_TRUE(Allowed("sin(\"a.fits:nomask\") + pi() + indexin(0, [1:5])", top));
    // The top-level folder written through a symlink
    EXPECT_TRUE(Allowed("\"" + (root / "alias" / "a.fits").string() + "\"", top));
    EXPECT_TRUE(Allowed("\"" + (root / "alias" / "sub" / ".." / "a.fits").string() + "\"", top));
}

TEST_F(CheckExpressionNamesTest, RefusesNamesOutsideFolder) {
    EXPECT_FALSE(Allowed("\"../a.fits\"", top));
    EXPECT_FALSE(Allowed("\"" + (outside / "a.fits").string() + "\"", top));
    EXPECT_FALSE(Allowed("a.fits + \\.\\.\\/a.fits", top));
    EXPECT_FALSE(Allowed("'../'\"a.fits\"", top));

    setenv("CARTA_TEST_PARENT", "..", 1);
    EXPECT_FALSE(Allowed("\"$CARTA_TEST_PARENT/a.fits\"", top));
    unsetenv("CARTA_TEST_PARENT");

    // casacore only splits a name at an unescaped colon, so this is one path that climbs out through b:c
    EXPECT_FALSE(Allowed("\"../b\\:c/../../a.fits\"", top / "sub"));
    // casacore reads the 1 as a number, which leaves /tmp/a.fits as the name
    EXPECT_FALSE(Allowed("1\\/tmp\\/a.fits", top));
    EXPECT_FALSE(Allowed("a.fits:1\\/tmp\\/a.fits", top));
    // With no image name casacore opens its working directory
    EXPECT_FALSE(Allowed(":nomask + \"a.fits\"", top));
    EXPECT_FALSE(Allowed("\":nomask\"", top));
}

TEST_F(CheckExpressionNamesTest, FollowsSymlinksOnlyDownwards) {
    // Sites link shared data into users' folders, so a name can go through a link that points outside
    EXPECT_TRUE(Allowed("\"link/a.fits\"", top));
    EXPECT_TRUE(Allowed("\"../link/a.fits\"", top / "sub"));
    EXPECT_TRUE(Allowed("\"" + (top / "link" / "a.fits").string() + "\"", top));
    EXPECT_TRUE(Allowed("\"../a.fits\"", top / "link" / "run1"));
    EXPECT_TRUE(Allowed("\"missing/../a.fits\"", top / "link" / "run1"));
    // A .. starts from where the directory really is, which here is two levels down
    EXPECT_TRUE(Allowed("\"../../a.fits\"", top / "latest"));
    // A saved expression is opened from where its symlinks lead
    EXPECT_TRUE(Allowed("\"a.fits\"", outside));

    // The OS follows the link before the .. so these leave the folder
    EXPECT_FALSE(Allowed("\"link/../a.fits\"", top));
    EXPECT_FALSE(Allowed("\"../../a.fits\"", top / "link" / "run1"));
    EXPECT_FALSE(Allowed("\"../a.fits\"", outside));
    // The OS stays inside here, but casacore opens a table from the text, where the two .. leave the folder
    EXPECT_FALSE(Allowed("\"latest/../../a.fits\"", top));
}

TEST_F(CheckExpressionNamesTest, RefusesNameThatCannotBeResolved) {
    EXPECT_FALSE(Allowed("\"loop/../a.fits\"", top));
}

TEST_F(CheckExpressionNamesTest, RefusesSavedExpression) {
    fs::create_directory(top / "saved.im");
    CreateFile(top / "saved.im" / "imageexpr.json");
    fs::create_directory(top / "e:y");
    CreateFile(top / "e:y" / "imageexpr.json");

    EXPECT_FALSE(Allowed("\"saved.im\" * 2", top));
    // The backslash left by the first unescape hides the colon from the second, so this is the one name e:y
    EXPECT_FALSE(Allowed("e\\\\:y * 2", top));
    // With the default top-level folder expressions are left alone
    EXPECT_NO_THROW(CheckExpressionNames("\"saved.im\" * 2", top.string(), "/"));
}

TEST(NormalizeUnitTest, NormalizeCommonUnits) {
    casacore::String unit = "JY";
    NormalizeUnit(unit);
    EXPECT_EQ(unit, "Jy");

    unit = "beam-1 Jy";
    NormalizeUnit(unit);
    EXPECT_EQ(unit, "Jy/beam");

    unit = "DEGREE";
    NormalizeUnit(unit);
    EXPECT_EQ(unit, "deg");
}

TEST(UtilTest, CheckGildasUnit) {
    EXPECT_TRUE(IsGildasUnit("K (Ta*)"));
    EXPECT_TRUE(IsGildasUnit("K (Tmb)"));
    EXPECT_TRUE(IsGildasUnit("Jy (Tb)"));
    EXPECT_TRUE(IsGildasUnit("K.(Ta.)"));
    EXPECT_TRUE(IsGildasUnit("K.(Tmb)"));
    EXPECT_FALSE(IsGildasUnit("K"));
    EXPECT_FALSE(IsGildasUnit("Jy"));
}

TEST(UtilTest, FolderImageType) {
    auto pwd = ImagesPath();
    std::string message;
    // Image files are unknown folder image types
    EXPECT_EQ(FolderImageType((pwd / "mix/M17_SWex_unit.fits").string(), message), CARTA::FileType::UNKNOWN);
    EXPECT_EQ(FolderImageType((pwd / "mix/M17_SWex_unit.hdf5").string(), message), CARTA::FileType::UNKNOWN);
    // Image folders have folder image type
    EXPECT_EQ(FolderImageType((pwd / "mix/M17_SWex_unit.image").string(), message), CARTA::FileType::CASA);
    EXPECT_EQ(FolderImageType((pwd / "mix/M17_SWex_unit.miriad").string(), message), CARTA::FileType::MIRIAD);
}
