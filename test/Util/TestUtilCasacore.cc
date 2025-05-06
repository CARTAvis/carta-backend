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
    std::string resolved = GetResolvedFilename(pwd.string(), "data/images/fits", "noise_4d.fits", message);

    EXPECT_FALSE(resolved.empty());
    EXPECT_TRUE(message.empty());
}

TEST_F(GetResolvedFilenameTest, FileDoesNotExist) {
    std::string message;
    std::string resolved = GetResolvedFilename("/", "test_dir", "missing.txt", message);

    EXPECT_TRUE(resolved.empty());
    EXPECT_FALSE(message.empty());
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

TEST(ParseHistoryBeamHeaderTest, ValidHistoryBeamFormat) {
    std::string header = "HISTORY RESTOR Beam = 2.000E+00 x 1.800E+00 arcsec, pa = 8.000E+01 degrees";
    std::string bmaj, bmin, bpa;

    EXPECT_TRUE(ParseHistoryBeamHeader(header, bmaj, bmin, bpa));
    EXPECT_EQ(bmaj, "2.000E+00arcsec");
    EXPECT_EQ(bmin, "1.800E+00arcsec");
    EXPECT_EQ(bpa, "8.000E+01deg");
}

TEST(ParseHistoryBeamHeaderTest, InvalidHistoryBeamFormat) {
    std::string header = "Invalid format";
    std::string bmaj, bmin, bpa;

    EXPECT_FALSE(ParseHistoryBeamHeader(header, bmaj, bmin, bpa));
}

TEST(ParseHistoryBeamHeaderTest, ValidGaussianBeam) {
    casacore::GaussianBeam beam(casacore::Quantity(1.5, "arcsec"), casacore::Quantity(1.2, "arcsec"), casacore::Quantity(45, "deg"));

    std::string formatted = FormatBeam(beam);
    EXPECT_NE(formatted.find("major: 1.500000 arcsec"), std::string::npos);
    EXPECT_NE(formatted.find("minor: 1.200000 arcsec"), std::string::npos);
    EXPECT_NE(formatted.find("pa: 45.000000 deg"), std::string::npos);
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
