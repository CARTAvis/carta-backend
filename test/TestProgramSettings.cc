/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "SessionManager/ProgramSettings.h"

#ifdef _BOOST_FILESYSTEM_
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

using namespace std;

const carta::ProgramSettings default_settings;

TEST(ProgramSettings, DefaultConstructor) {
    carta::ProgramSettings settings;
    EXPECT_FALSE(settings.help);
    EXPECT_FALSE(settings.version);
    EXPECT_FALSE(settings.no_http);
    EXPECT_FALSE(settings.no_log);
    EXPECT_FALSE(settings.no_browser);
    EXPECT_FALSE(settings.debug_no_auth);

    EXPECT_TRUE(settings.frontend_folder.empty());
    EXPECT_TRUE(settings.files.empty());

    EXPECT_EQ(settings.port, -1);
    EXPECT_EQ(settings.grpc_port, -1);
    EXPECT_EQ(settings.omp_thread_count, -1);
    EXPECT_EQ(settings.top_level_folder, "/");
    EXPECT_EQ(settings.starting_folder, ".");
    EXPECT_EQ(settings.host, "0.0.0.0");
    EXPECT_EQ(settings.verbosity, 4);
    EXPECT_EQ(settings.wait_time, -1);
    EXPECT_EQ(settings.init_wait_time, -1);
}

// Utility for converting vector of string values to
auto SettingsFromVector(vector<string> argVector) {
    std::vector<char*> cstrings;
    cstrings.reserve(argVector.size());

    for (auto& s : argVector)
        cstrings.push_back(&s[0]);
    carta::ProgramSettings settings(argVector.size(), cstrings.data());

    return std::move(settings);
}

TEST(ProgramSettings, EmptyArugments) {
    auto settings = SettingsFromVector({"carta_backend"});
    EXPECT_TRUE(settings == default_settings);
    settings = SettingsFromVector({"carta_backend", ""});
    EXPECT_TRUE(settings == default_settings);
}

TEST(ProgramSettings, OverrideDeprecatedRoot) {
    auto settings = SettingsFromVector({"carta_backend", "--root", "/tmp2", "--top_level_folder", "/tmp"});
    EXPECT_EQ(settings.top_level_folder, "/tmp");
    settings = SettingsFromVector({"carta_backend", "--top_level_folder", "/tmp", "--root", "/tmp2"});
    EXPECT_EQ(settings.top_level_folder, "/tmp");
}

TEST(ProgramSettings, OverrideDeprecatedBase) {
    auto settings = SettingsFromVector({"carta_backend", "--base", "/tmp2", "/tmp"});
    EXPECT_EQ(settings.starting_folder, "/tmp");
    auto image_dir = fs::current_path() / "data/images";
    settings = SettingsFromVector({"carta_backend", "--base", "/tmp2", image_dir.string()});
    EXPECT_EQ(settings.starting_folder, image_dir.string());
}

TEST(ProgramSettings, StartingFolderFromPositional) {
    auto image_dir = fs::current_path() / "data/images";
    auto settings = SettingsFromVector({"carta_backend", image_dir.string()});
    EXPECT_EQ(settings.starting_folder, image_dir.string());
    EXPECT_TRUE(settings.files.empty());
}

TEST(ProgramSettings, IgnoreInvalidFolder) {
    auto image_dir = fs::current_path() / "data/images_invalid";
    auto settings = SettingsFromVector({"carta_backend", image_dir.string()});
    EXPECT_EQ(settings.starting_folder, default_settings.starting_folder);
    EXPECT_TRUE(settings.files.empty());
}

TEST(ProgramSettings, IgnoreInvalidFile) {
    auto fits_image = fs::current_path() / "data/images/fits/invalid.fits";
    auto settings = SettingsFromVector({"carta_backend", fits_image.string()});
    EXPECT_EQ(settings.starting_folder, default_settings.starting_folder);
    EXPECT_TRUE(settings.files.empty());
}

TEST(ProgramSettings, FileImageFromPositional) {
    auto image_dir = fs::current_path() / "data/images";
    auto fits_image = image_dir / "fits/noise_10px_10px.fits";
    auto settings = SettingsFromVector({"carta_backend", fits_image.string()});
    EXPECT_EQ(settings.starting_folder, default_settings.starting_folder);
    EXPECT_EQ(settings.files.size(), 1);
    EXPECT_EQ(settings.files[0], fits_image.string());

    auto hdf5_image = image_dir / "hdf5/noise_10px_10px.hdf5";
    settings = SettingsFromVector({"carta_backend", hdf5_image.string()});
    EXPECT_EQ(settings.files.size(), 1);
    EXPECT_EQ(settings.files[0], hdf5_image.string());
}

TEST(ProgramSettings, CasaImageSetFromPositional) {
    auto image_dir = fs::current_path() / "data/images";
    auto casa_image = image_dir / "casa/noise_10px_10px.im";
    auto settings = SettingsFromVector({"carta_backend", casa_image.string()});
    EXPECT_EQ(settings.starting_folder, default_settings.starting_folder);
    EXPECT_EQ(settings.files.size(), 1);
    EXPECT_EQ(settings.files[0], casa_image.string());
}

TEST(ProgramSettings, MultipleImagesFromPositional) {
    auto image_dir = fs::current_path() / "data/images";
    auto casa_image = image_dir / "casa/noise_10px_10px.im";
    auto fits_image = image_dir / "fits/noise_10px_10px.fits";
    auto hdf5_image = image_dir / "hdf5/noise_10px_10px.hdf5";
    auto settings = SettingsFromVector({"carta_backend", fits_image.string(), casa_image.string(), hdf5_image.string()});
    EXPECT_EQ(settings.files.size(), 3);
    EXPECT_EQ(settings.files[0], fits_image.string());
    EXPECT_EQ(settings.files[1], casa_image.string());
    EXPECT_EQ(settings.files[1], hdf5_image.string());

    settings = SettingsFromVector({"carta_backend", casa_image.string(), fits_image.string(), hdf5_image.string()});
    EXPECT_EQ(settings.files.size(), 3);
    EXPECT_EQ(settings.files[0], casa_image.string());
    EXPECT_EQ(settings.files[1], fits_image.string());
    EXPECT_EQ(settings.files[2], hdf5_image.string());
}