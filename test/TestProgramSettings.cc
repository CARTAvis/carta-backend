/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>
#include <cxxopts/cxxopts.hpp>

#include "CommonTestUtilities.h"
#include "HttpServer/HttpServer.h"
#include "Main/ProgramSettings.h"
#include "Util/String.h"

#include <fstream>
#include <iostream>

#define GTEST_COUT std::cerr << "[          ] [ DEBUG ]"

fs::path SettingsPath() {
    return TestRoot() / "data" / "settings";
}

class ProgramSettingsTest : public ::testing::Test {
public:
    carta::ProgramSettings default_settings;

    // Utility for converting vector of string values to
    static auto SettingsFromVector(std::vector<std::string> argVector) {
        std::vector<char*> cstrings;
        cstrings.reserve(argVector.size());

        for (auto& s : argVector) {
            cstrings.push_back(&s[0]);
        }

        carta::ProgramSettings settings;
        settings.ApplyCommandLineSettings(argVector.size(), cstrings.data());
        settings.PushFilePaths();
        return std::move(settings);
    }

    static auto SettingsFromString(const std::string& argString) {
        std::vector<std::string> argVector;

        std::string token;
        std::istringstream stream(argString);
        while (std::getline(stream, token, ' ')) {
            argVector.push_back(token);
        }
        return SettingsFromVector(argVector);
    }

    void SetUp() {
        working_directory = fs::current_path();
    }

    void TearDown() {
        fs::current_path(working_directory);
    }

    static void CheckConsistency(const ProgramSettings& p1, const ProgramSettings& p2) {
        EXPECT_TRUE(p1.version == p2.version);
        EXPECT_TRUE(p1.help == p2.help);
        if (!p1.port.empty() || !p2.port.empty()) {
            EXPECT_TRUE(p1.port == p2.port);
        }
        EXPECT_TRUE(p1.omp_thread_count == p2.omp_thread_count);
        EXPECT_TRUE(p1.event_thread_count == p2.event_thread_count);
        EXPECT_TRUE(p1.top_level_folder == p2.top_level_folder);
        EXPECT_TRUE(p1.starting_folder == p2.starting_folder);
        EXPECT_TRUE(p1.host == p2.host);
        if (!p1.files.empty() || !p2.files.empty()) {
            EXPECT_TRUE(p1.files == p2.files);
        }
        if (!p1.file_paths.empty() || !p2.file_paths.empty()) {
            EXPECT_TRUE(p1.file_paths == p2.file_paths);
        }
        EXPECT_TRUE(p1.frontend_folder == p2.frontend_folder);
        EXPECT_TRUE(p1.no_http == p2.no_http);
        EXPECT_TRUE(p1.no_frontend == p2.no_frontend);
        EXPECT_TRUE(p1.no_database == p2.no_database);
        EXPECT_TRUE(p1.no_runtime_config == p2.no_runtime_config);
        EXPECT_TRUE(p1.debug_no_auth == p2.debug_no_auth);
        EXPECT_TRUE(p1.no_browser == p2.no_browser);
        EXPECT_TRUE(p1.no_log == p2.no_log);
        EXPECT_TRUE(p1.log_performance == p2.log_performance);
        EXPECT_TRUE(p1.log_protocol_messages == p2.log_protocol_messages);
        EXPECT_TRUE(p1.verbosity == p2.verbosity);
        EXPECT_TRUE(p1.wait_time == p2.wait_time);
        EXPECT_TRUE(p1.init_wait_time == p2.init_wait_time);
        EXPECT_TRUE(p1.idle_session_wait_time == p2.idle_session_wait_time);
        EXPECT_TRUE(p1.read_only_mode == p2.read_only_mode);
        EXPECT_TRUE(p1.enable_scripting == p2.enable_scripting);
        EXPECT_TRUE(p1.controller_deployment == p2.controller_deployment);
        EXPECT_TRUE(p1.browser == p2.browser);
        EXPECT_TRUE(p1.no_user_config == p2.no_user_config);
        EXPECT_TRUE(p1.no_system_config == p2.no_system_config);
        if (!p1.command_line_settings.empty() || !p2.command_line_settings.empty()) {
            EXPECT_TRUE(p1.command_line_settings == p2.command_line_settings);
        }
        EXPECT_TRUE(p1.system_settings_json_exists == p2.system_settings_json_exists);
        EXPECT_TRUE(p1.user_settings_json_exists == p2.user_settings_json_exists);
        EXPECT_TRUE(p1.user_directory == p2.user_directory);
        if (!p1.warning_msgs.empty() || !p2.warning_msgs.empty()) {
            EXPECT_TRUE(p1.warning_msgs == p2.warning_msgs);
        }
        std::vector<std::string> debug_msgs;
        if (!p1.debug_msgs.empty() || !p2.debug_msgs.empty()) {
            EXPECT_TRUE(p1.debug_msgs == p2.debug_msgs);
        }
    }

private:
    fs::path working_directory;
};

TEST_F(ProgramSettingsTest, DefaultConstructor) {
    carta::ProgramSettings settings;
    EXPECT_FALSE(settings.help);
    EXPECT_FALSE(settings.version);
    EXPECT_FALSE(settings.no_http);
    EXPECT_FALSE(settings.no_log);
    EXPECT_FALSE(settings.no_browser);
    EXPECT_FALSE(settings.debug_no_auth);
    EXPECT_FALSE(settings.read_only_mode);
    EXPECT_FALSE(settings.enable_scripting);

    EXPECT_TRUE(settings.frontend_folder.empty());
    EXPECT_TRUE(settings.files.empty());

    EXPECT_EQ(settings.port.size(), 0);
    EXPECT_EQ(settings.omp_thread_count, -1);
    EXPECT_EQ(settings.top_level_folder, "/");
    EXPECT_EQ(settings.starting_folder, ".");
    EXPECT_EQ(settings.host, "0.0.0.0");
    EXPECT_EQ(settings.verbosity, 4);
    EXPECT_EQ(settings.wait_time, -1);
    EXPECT_EQ(settings.init_wait_time, -1);
    EXPECT_EQ(settings.idle_session_wait_time, -1);
}

TEST_F(ProgramSettingsTest, EmptyArugments) {
    auto settings = SettingsFromVector({"carta_backend"});
    CheckConsistency(settings, default_settings);
    settings = SettingsFromVector({"carta_backend", ""});
    CheckConsistency(settings, default_settings);
    ASSERT_THROW(settings = SettingsFromVector({"carta_backend", "--top_level_folder"}), cxxopts::OptionException);
}

TEST_F(ProgramSettingsTest, ExpectedValuesLong) {
    auto settings = SettingsFromString(
        "carta_backend --verbosity 6 --no_log --no_http --no_browser --host helloworld --port 1234 --omp_threads 10"
        " --top_level_folder /tmp --frontend_folder /var --exit_timeout 10 --initial_timeout 11 --debug_no_auth --read_only_mode "
        "--enable_scripting");
    EXPECT_EQ(settings.verbosity, 6);
    EXPECT_EQ(settings.no_log, true);
    EXPECT_EQ(settings.no_http, true);
    EXPECT_EQ(settings.no_browser, true);
    EXPECT_EQ(settings.host, "helloworld");
    EXPECT_EQ(settings.port[0], 1234);
    EXPECT_EQ(settings.omp_thread_count, 10);
    EXPECT_EQ(settings.top_level_folder, "/tmp");
    EXPECT_EQ(settings.frontend_folder, "/var");
    EXPECT_EQ(settings.wait_time, 10);
    EXPECT_EQ(settings.init_wait_time, 11);
    EXPECT_EQ(settings.debug_no_auth, true);
    EXPECT_EQ(settings.read_only_mode, true);
    EXPECT_EQ(settings.enable_scripting, true);
}

TEST_F(ProgramSettingsTest, ExpectedValuesShort) {
    auto settings = SettingsFromString("carta_backend -p 1234 -t 10");
    EXPECT_EQ(settings.port[0], 1234);
    EXPECT_EQ(settings.omp_thread_count, 10);
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
    settings = SettingsFromVector({"carta_backend", "--base", "/tmp2", ImagesPath()});
    EXPECT_EQ(settings.starting_folder, ImagesPath());
}

TEST_F(ProgramSettingsTest, StartingFolderFromPositional) {
    auto file_path = ImagesPath();
    auto settings = SettingsFromVector({"carta_backend", file_path});
    EXPECT_EQ(settings.starting_folder, file_path);
    EXPECT_TRUE(settings.files.empty());
}

TEST_F(ProgramSettingsTest, IgnoreInvalidFolder) {
    auto image_dir = TestRoot() / "data" / "images_invalid";
    auto settings = SettingsFromVector({"carta_backend", image_dir});
    EXPECT_EQ(settings.starting_folder, default_settings.starting_folder);
    EXPECT_TRUE(settings.files.empty());
}

TEST_F(ProgramSettingsTest, IgnoreInvalidFile) {
    auto fits_image_path = FitsImages() / "invalid.fits";
    auto settings = SettingsFromVector({"carta_backend", fits_image_path});
    EXPECT_EQ(settings.starting_folder, default_settings.starting_folder);
    EXPECT_TRUE(settings.files.empty());
}

TEST_F(ProgramSettingsTest, FileImageFromPositional) {
    auto fits_image_path = FitsImages() / "noise_10px_10px.fits";
    auto settings = SettingsFromVector({"carta_backend", fits_image_path});
    EXPECT_EQ(settings.starting_folder, default_settings.starting_folder);
    ASSERT_EQ(settings.files.size(), 1);
    EXPECT_EQ(settings.files[0], fs::relative(fits_image_path, "/"));
}

TEST_F(ProgramSettingsTest, RelativeFileImageFromPositional) {
    auto absolute_image_path = FitsImages() / "noise_10px_10px.fits";
    fs::current_path(TestRoot());
    std::string relative_image_path = fs::relative(absolute_image_path);
    auto settings = SettingsFromVector({"carta_backend", relative_image_path});
    ASSERT_EQ(settings.files.size(), 1);
    EXPECT_EQ(settings.files[0], fs::relative(absolute_image_path, "/"));
}

TEST_F(ProgramSettingsTest, TrimExtraFolders) {
    auto absolute_image_path = FitsImages() / "noise_10px_10px.fits";
    fs::current_path(TestRoot());
    std::string relative_image_path = "./data/images/fits/noise_10px_10px.fits";
    auto settings = SettingsFromVector({"carta_backend", relative_image_path});
    ASSERT_EQ(settings.files.size(), 1);
    EXPECT_EQ(settings.files[0], fs::relative(absolute_image_path, "/"));
}

TEST_F(ProgramSettingsTest, FileImageRelativeToTopLevel) {
    auto top_level_path = ImagesPath();
    fs::current_path(TestRoot());

    std::string relative_image_path = "data/images/fits/noise_10px_10px.fits";
    auto settings = SettingsFromVector({"carta_backend", "--top_level_folder", top_level_path, relative_image_path});
    ASSERT_EQ(settings.files.size(), 1);
    EXPECT_EQ(settings.files[0], "fits/noise_10px_10px.fits");

    relative_image_path = "./data/images/fits/noise_10px_10px.fits";
    settings = SettingsFromVector({"carta_backend", "--top_level_folder", top_level_path, relative_image_path});
    ASSERT_EQ(settings.files.size(), 1);
    EXPECT_EQ(settings.files[0], "fits/noise_10px_10px.fits");

    relative_image_path = "../test/data/images/fits/noise_10px_10px.fits";
    settings = SettingsFromVector({"carta_backend", "--top_level_folder", top_level_path, relative_image_path});
    ASSERT_EQ(settings.files.size(), 1);
    EXPECT_EQ(settings.files[0], "fits/noise_10px_10px.fits");
}

TEST_F(ProgramSettingsTest, CasaImageSetFromPositional) {
    auto casa_image_path = CasaImages() / "noise_10px_10px.im";
    auto settings = SettingsFromVector({"carta_backend", casa_image_path});
    EXPECT_EQ(settings.files.size(), 1);
    EXPECT_EQ(settings.files[0], fs::relative(casa_image_path, "/"));
}

TEST_F(ProgramSettingsTest, MultipleImagesFromPositional) {
    auto casa_image_path = CasaImages() / "noise_10px_10px.im";
    auto fits_image_path = FitsImages() / "noise_10px_10px.fits";
    auto hdf5_image_path = Hdf5Images() / "noise_10px_10px.hdf5";

    auto settings = SettingsFromVector({"carta_backend", fits_image_path, casa_image_path, hdf5_image_path});
    ASSERT_EQ(settings.files.size(), 3);
    EXPECT_EQ(settings.files[0], fs::relative(fits_image_path, "/"));
    EXPECT_EQ(settings.files[1], fs::relative(casa_image_path, "/"));
    EXPECT_EQ(settings.files[2], fs::relative(hdf5_image_path, "/"));

    settings = SettingsFromVector({"carta_backend", casa_image_path, fits_image_path, hdf5_image_path});
    ASSERT_EQ(settings.files.size(), 3);
    EXPECT_EQ(settings.files[0], fs::relative(casa_image_path, "/"));
    EXPECT_EQ(settings.files[1], fs::relative(fits_image_path, "/"));
    EXPECT_EQ(settings.files[2], fs::relative(hdf5_image_path, "/"));
}

TEST_F(ProgramSettingsTest, ExpectedValuesLongJSON) {
    auto json_string = R"(
    {
        "verbosity": 6,
        "no_log": true,
        "no_http": true,
        "no_browser": true,
        "host": "helloworld",
        "port": [1234],
        "omp_threads": 10,
        "top_level_folder": "/tmp",
        "frontend_folder": "/var",
        "exit_timeout": 10,
        "initial_timeout": 11,
        "read_only_mode": true,
        "enable_scripting": true
    })";
    nlohmann::json j = nlohmann::json::parse(json_string);

    carta::ProgramSettings settings;
    settings.SetSettingsFromJSON(j);

    EXPECT_EQ(settings.verbosity, 6);
    EXPECT_EQ(settings.no_log, true);
    EXPECT_EQ(settings.no_http, true);
    EXPECT_EQ(settings.no_browser, true);
    EXPECT_EQ(settings.host, "helloworld");
    EXPECT_EQ(settings.port[0], 1234);
    EXPECT_EQ(settings.omp_thread_count, 10);
    EXPECT_EQ(settings.top_level_folder, "/tmp");
    EXPECT_EQ(settings.frontend_folder, "/var");
    EXPECT_EQ(settings.wait_time, 10);
    EXPECT_EQ(settings.init_wait_time, 11);
    EXPECT_EQ(settings.read_only_mode, true);
    EXPECT_EQ(settings.enable_scripting, true);
}

TEST_F(ProgramSettingsTest, ValidateJSONFromFileWithGoodFields) {
    auto input = SettingsPath() / "settings-good-fields.json";
    carta::ProgramSettings settings;
    auto j = settings.JSONSettingsFromFile(input);
    EXPECT_EQ(j.size(), 13);
    EXPECT_EQ(j["verbosity"], 5);
    EXPECT_EQ(j["port"][0], 1234);
    EXPECT_EQ(j["omp_threads"], 10);
    EXPECT_EQ(j["exit_timeout"], 10);
    EXPECT_EQ(j["initial_timeout"], 11);
    EXPECT_EQ(j["no_log"], true);
    EXPECT_EQ(j["no_http"], true);
    EXPECT_EQ(j["no_browser"], true);
    EXPECT_EQ(j["host"], "helloworld");
    EXPECT_EQ(j["top_level_folder"], "/tmp");
    EXPECT_EQ(j["frontend_folder"], "/var");
    EXPECT_EQ(j["read_only_mode"], true);
    EXPECT_EQ(j["enable_scripting"], true);
}

TEST_F(ProgramSettingsTest, ValidateJSONFromFileWithBadFields) {
    fs::current_path(TestRoot());
    const std::string input = SettingsPath() / "settings-bad-fields.json";
    carta::ProgramSettings settings;
    auto j = settings.JSONSettingsFromFile(input);
    settings.SetSettingsFromJSON(j);
    EXPECT_EQ(j.size(), 0);
}

TEST_F(ProgramSettingsTest, TestValuesFromGoodSettings) {
    auto input = SettingsPath() / "settings-good-fields.json";
    carta::ProgramSettings settings;
    auto j = settings.JSONSettingsFromFile(input);
    settings.SetSettingsFromJSON(j);
    EXPECT_EQ(settings.verbosity, 5);
    EXPECT_EQ(settings.no_log, true);
    EXPECT_EQ(settings.no_http, true);
    EXPECT_EQ(settings.no_browser, true);
    EXPECT_EQ(settings.host, "helloworld");
    EXPECT_EQ(settings.port[0], 1234);
    EXPECT_EQ(settings.omp_thread_count, 10);
    EXPECT_EQ(settings.top_level_folder, "/tmp");
    EXPECT_EQ(settings.frontend_folder, "/var");
    EXPECT_EQ(settings.wait_time, 10);
    EXPECT_EQ(settings.init_wait_time, 11);
    EXPECT_EQ(settings.read_only_mode, true);
    EXPECT_EQ(settings.enable_scripting, true);
}

TEST_F(ProgramSettingsTest, TestDefaultsFallbackFromBadSettings) {
    auto input = SettingsPath() / "settings-bad-fields.json";
    carta::ProgramSettings settings;
    auto j = settings.JSONSettingsFromFile(input);
    settings.SetSettingsFromJSON(j);
    EXPECT_EQ(settings.verbosity, 4);
    EXPECT_EQ(settings.no_log, false);
    EXPECT_EQ(settings.no_http, false);
    EXPECT_EQ(settings.no_browser, false);
    EXPECT_EQ(settings.host, "0.0.0.0");
    EXPECT_EQ(settings.port.size(), 0);
    EXPECT_EQ(settings.omp_thread_count, -1);
    EXPECT_EQ(settings.top_level_folder, "/");
    EXPECT_EQ(settings.frontend_folder, "");
    EXPECT_EQ(settings.wait_time, -1);
    EXPECT_EQ(settings.init_wait_time, -1);
    EXPECT_EQ(settings.read_only_mode, false);
    EXPECT_EQ(settings.enable_scripting, false);
}

TEST_F(ProgramSettingsTest, TestFileQueryStringEmptyFiles) {
    std::vector<std::string> files;
    auto url_string = carta::HttpServer::GetFileUrlString(files);
    EXPECT_EQ(url_string, "");
}

TEST_F(ProgramSettingsTest, TestFileQueryStringSingleFile) {
    auto file_path = FitsImages() / "noise_3d.fits";
    std::vector<std::string> files;
    files.push_back(file_path);
    auto url_string = carta::HttpServer::GetFileUrlString(files);
    EXPECT_EQ(url_string, fmt::format("file={}", SafeStringEscape(file_path)));
}

TEST_F(ProgramSettingsTest, TestFileQueryStringTwoFilesSameFolder) {
    std::vector<std::string> files;
    files.push_back(FitsImages() / "noise_3d.fits");
    files.push_back(FitsImages() / "noise_4d.fits");
    auto folder = SafeStringEscape(FitsImages());

    auto url_string = carta::HttpServer::GetFileUrlString(files);
    EXPECT_EQ(url_string, fmt::format("folder={}&files={}", folder,
                              "noise_3d.fits,10x10x10x10_nan-row-column_nanpct-10.0_seed-0.fits"));
}

TEST_F(ProgramSettingsTest, TestFileQueryStringTwoFilesDifferentFolder) {
    std::vector<std::string> files;
    files.push_back(FitsImages() / "noise_3d.fits");
    files.push_back(Hdf5Images() / "noise_10px_10px.hdf5");
    auto fits_file = SafeStringEscape(fs::absolute(FitsImages() / "noise_3d.fits"));
    auto hdf5_file = SafeStringEscape(fs::absolute(Hdf5Images() / "noise_10px_10px.hdf5"));

    auto url_string = carta::HttpServer::GetFileUrlString(files);
    EXPECT_EQ(url_string, fmt::format("files={},{}", fits_file, hdf5_file));
}
