/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>
#include <cxxopts.hpp>

#include "SessionManager/ProgramSettings.h"

#ifdef _BOOST_FILESYSTEM_
#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

using namespace std;

class ProgramSettingsTest : public ::testing::Test {
public:
    carta::ProgramSettings default_settings;

    // Utility for converting vector of string values to
    static auto SettingsFromVector(vector<string> argVector) {
        std::vector<char*> cstrings;
        cstrings.reserve(argVector.size());

        for (auto& s : argVector) {
            cstrings.push_back(&s[0]);
        }
        carta::ProgramSettings settings(argVector.size(), cstrings.data());
        return std::move(settings);
    }
};

TEST_F(ProgramSettingsTest, DefaultConstructor) {
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

TEST_F(ProgramSettingsTest, EmptyArugments) {
    auto settings = SettingsFromVector({"carta_backend"});
    EXPECT_TRUE(settings == default_settings);
    settings = SettingsFromVector({"carta_backend", ""});
    EXPECT_TRUE(settings == default_settings);
    ASSERT_THROW(settings = SettingsFromVector({"carta_backend", "--top_level_folder"}), cxxopts::OptionException);
}

TEST_F(ProgramSettingsTest, OverrideDeprecatedRoot) {
    auto settings = SettingsFromVector({"carta_backend", "--root", "/tmp2", "--top_level_folder", "/tmp"});
    EXPECT_EQ(settings.top_level_folder, "/tmp");
    settings = SettingsFromVector({"carta_backend", "--top_level_folder", "/tmp", "--root", "/tmp2"});
    EXPECT_EQ(settings.top_level_folder, "/tmp");
}

TEST_F(ProgramSettingsTest, OverrideDeprecatedBase) {
    auto settings = SettingsFromVector({"carta_backend", "--base", "/tmp2", "/tmp"});
    EXPECT_EQ(settings.starting_folder, "/tmp");
    auto image_dir = fs::current_path() / "data/images";
    settings = SettingsFromVector({"carta_backend", "--base", "/tmp2", image_dir.string()});
    EXPECT_EQ(settings.starting_folder, image_dir.string());
}

TEST_F(ProgramSettingsTest, StartingFolderFromPositional) {
    auto image_dir = fs::current_path() / "data/images";
    auto settings = SettingsFromVector({"carta_backend", image_dir.string()});
    EXPECT_EQ(settings.starting_folder, image_dir.string());
    EXPECT_TRUE(settings.files.empty());
}

TEST_F(ProgramSettingsTest, IgnoreInvalidFolder) {
    auto image_dir = fs::current_path() / "data/images_invalid";
    auto settings = SettingsFromVector({"carta_backend", image_dir.string()});
    EXPECT_EQ(settings.starting_folder, default_settings.starting_folder);
    EXPECT_TRUE(settings.files.empty());
}

TEST_F(ProgramSettingsTest, IgnoreInvalidFile) {
    auto fits_image = fs::current_path() / "data/images/fits/invalid.fits";
    auto settings = SettingsFromVector({"carta_backend", fits_image.string()});
    EXPECT_EQ(settings.starting_folder, default_settings.starting_folder);
    EXPECT_TRUE(settings.files.empty());
}

TEST_F(ProgramSettingsTest, FileImageFromPositional) {
    auto image_dir = fs::current_path() / "data/images";
    auto fits_image = image_dir / "fits/noise_10px_10px.fits";
    auto settings = SettingsFromVector({"carta_backend", fits_image.string()});
    EXPECT_EQ(settings.starting_folder, default_settings.starting_folder);
    ASSERT_EQ(settings.files.size(), 1);
    // substr to remove leading "/" from expected path
    EXPECT_EQ(settings.files[0], fits_image.string().substr(1));
}

TEST_F(ProgramSettingsTest, RelativeFileImageFromPositional) {
    auto image_dir = fs::current_path() / "data/images";
    string image_path_string = "data/images/fits/noise_10px_10px.fits";
    auto image_path = image_dir / "fits/noise_10px_10px.fits";
    auto settings = SettingsFromVector({"carta_backend", image_path_string});
    ASSERT_EQ(settings.files.size(), 1);
    EXPECT_EQ(settings.files[0], image_path.string().substr(1));
}

TEST_F(ProgramSettingsTest, TrimExtraFolders) {
    auto image_dir = fs::current_path() / "data/images";
    string image_path_string = "./data/images/fits/noise_10px_10px.fits";
    auto image_path = image_dir / "fits/noise_10px_10px.fits";
    auto settings = SettingsFromVector({"carta_backend", image_path_string});
    ASSERT_EQ(settings.files.size(), 1);
    EXPECT_EQ(settings.files[0], image_path.string().substr(1));
}

TEST_F(ProgramSettingsTest, FileImageRelativeToTopLevel) {
    auto top_level_dir = fs::current_path() / "data/images";

    string image_path_string = "data/images/fits/noise_10px_10px.fits";
    auto settings = SettingsFromVector({"carta_backend", "--top_level_folder", top_level_dir.string(), image_path_string});
    ASSERT_EQ(settings.files.size(), 1);
    EXPECT_EQ(settings.files[0], "fits/noise_10px_10px.fits");

    image_path_string = "./data/images/fits/noise_10px_10px.fits";
    settings = SettingsFromVector({"carta_backend", "--top_level_folder", top_level_dir.string(), image_path_string});
    ASSERT_EQ(settings.files.size(), 1);
    EXPECT_EQ(settings.files[0], "fits/noise_10px_10px.fits");

    image_path_string = "../test/data/images/fits/noise_10px_10px.fits";
    settings = SettingsFromVector({"carta_backend", "--top_level_folder", top_level_dir.string(), image_path_string});
    ASSERT_EQ(settings.files.size(), 1);
    EXPECT_EQ(settings.files[0], "fits/noise_10px_10px.fits");
}

TEST_F(ProgramSettingsTest, CasaImageSetFromPositional) {
    auto image_dir = fs::current_path() / "data/images";
    auto casa_image = image_dir / "casa/noise_10px_10px.im";
    auto settings = SettingsFromVector({"carta_backend", casa_image.string()});
    EXPECT_EQ(settings.files.size(), 1);
    EXPECT_EQ(settings.files[0], casa_image.string().substr(1));
}

TEST_F(ProgramSettingsTest, MultipleImagesFromPositional) {
    auto image_dir = fs::current_path() / "data/images";
    auto casa_image = image_dir / "casa/noise_10px_10px.im";
    auto fits_image = image_dir / "fits/noise_10px_10px.fits";
    auto hdf5_image = image_dir / "hdf5/noise_10px_10px.hdf5";
    auto settings = SettingsFromVector({"carta_backend", fits_image.string(), casa_image.string(), hdf5_image.string()});
    ASSERT_EQ(settings.files.size(), 3);
    EXPECT_EQ(settings.files[0], fits_image.string().substr(1));
    EXPECT_EQ(settings.files[1], casa_image.string().substr(1));
    EXPECT_EQ(settings.files[2], hdf5_image.string().substr(1));

    settings = SettingsFromVector({"carta_backend", casa_image.string(), fits_image.string(), hdf5_image.string()});
    ASSERT_EQ(settings.files.size(), 3);
    EXPECT_EQ(settings.files[0], casa_image.string().substr(1));
    EXPECT_EQ(settings.files[1], fits_image.string().substr(1));
    EXPECT_EQ(settings.files[2], hdf5_image.string().substr(1));
}