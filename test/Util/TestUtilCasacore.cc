/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "Util/Casacore.h"

#include "CommonTestUtilities.h"

TEST(IsSubdirectoryTest, ValidSubdirectory) {
    EXPECT_TRUE(IsSubdirectory("/home/user/docs", "/home/user"));
    EXPECT_TRUE(IsSubdirectory("/var/log/apache", "/var/log"));
    EXPECT_TRUE(IsSubdirectory("/usr/local/bin", "/usr/local"));

    std::string top_level = "/tmp";
    std::string starting = "/tmp/test";

    fs::create_directories(starting); // Ensure directory exists

    EXPECT_TRUE(IsSubdirectory(starting, top_level));

    fs::remove(starting); // Cleanup
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
    EXPECT_TRUE(IsSubdirectory((pwd / "data").string(), pwd.string()));
    EXPECT_FALSE(IsSubdirectory(pwd.string(), (pwd / "data").string()));
    EXPECT_TRUE(IsSubdirectory((pwd / "data/images").string(), pwd.string()));
    EXPECT_FALSE(IsSubdirectory(pwd.string(), (pwd / "data/images").string()));
    EXPECT_TRUE(IsSubdirectory((pwd / "data/images").string(), (pwd / "data").string()));
    EXPECT_FALSE(IsSubdirectory((pwd / "data").string(), (pwd / "data/images").string()));
    EXPECT_TRUE(IsSubdirectory((pwd / "data/images/fits").string(), (pwd / "data/images").string()));
    EXPECT_FALSE(IsSubdirectory((pwd / "data/images/fits").string(), (pwd / "data/images/hdf5").string()));
}

TEST(IsSubdirectoryTest, SubdirectoryRel) {
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
    EXPECT_TRUE(IsSubdirectory("/tmp/test", "/tmp"));
    EXPECT_FALSE(IsSubdirectory("/etc", "/tmp"));
    EXPECT_TRUE(IsSubdirectory("/tmp/test/sub", "/tmp/test"));
}

TEST(CheckFolderPathsTest, ValidPaths) {
    std::string top_level = "/tmp";
    std::string starting = "/tmp/test";

    fs::create_directories(starting); // Ensure directory exists

    EXPECT_TRUE(CheckFolderPaths(top_level, starting));

    fs::remove(starting); // Cleanup
}

TEST(CheckFolderPathsTest, NonExistentStartingDirectory) {
    std::string top_level = "/tmp";
    std::string starting = "/tmp/nonexistent";

    EXPECT_TRUE(CheckFolderPaths(top_level, starting));
}

TEST(CheckFolderPathsTest, InvalidTopLevelDirectory) {
    std::string top_level = "/tmp/nonexistent";
    std::string starting = "/tmp/test";

    fs::create_directories(starting);

    EXPECT_FALSE(CheckFolderPaths(top_level, starting));

    fs::remove(starting);
}

TEST(CheckFolderPathsTest, TopLevelBase) {
    std::string top_level = "base";
    std::string starting = "/tmp/test";

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
    std::string top_level = "/tmp/parent";
    std::string starting = "/tmp/another";

    fs::create_directories(top_level);
    fs::create_directories(starting);

    EXPECT_FALSE(CheckFolderPaths(top_level, starting));

    fs::remove_all(top_level);
    fs::remove_all(starting);
}

TEST(CheckFolderPathsTest, SamePath) {
    std::string top_level = "/tmp/test_same";
    std::string starting = "/tmp/test_same";

    fs::create_directories(top_level);

    EXPECT_TRUE(CheckFolderPaths(top_level, starting));

    fs::remove(top_level);
}

TEST(GetResolvedFilenameTest, FileExists) {
    auto pwd = TestRoot();
    std::string message;
    std::string resolved = GetResolvedFilename(pwd.string(), "data/images/fits", "noise_4d.fits", message);

    EXPECT_FALSE(resolved.empty());
    EXPECT_TRUE(message.empty());
}

TEST(GetResolvedFilenameTest, FileDoesNotExist) {
    std::string message;
    std::string resolved = GetResolvedFilename("/tmp", "test_dir", "missing.txt", message);

    EXPECT_TRUE(resolved.empty());
    EXPECT_FALSE(message.empty());
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
