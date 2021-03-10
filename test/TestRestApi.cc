/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <chrono>
#include <fstream>
#include <thread>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "Logger/Logger.h"
#include "SimpleFrontendServer/SimpleFrontendServer.h"

#ifdef _BOOST_FILESYSTEM_
#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;
#else
#include <filesystem>

namespace fs = std::filesystem;
#endif

using namespace std;
using json = nlohmann::json;

// Allows testing of protected methods in SimpleFrontendServer without polluting the original class
class TestSimpleFrontendServer : public carta::SimpleFrontendServer {
public:
    TestSimpleFrontendServer(fs::path root_folder, std::string auth_token) : carta::SimpleFrontendServer(root_folder, auth_token) {}
    FRIEND_TEST(RestApiTest, UpdatePreferencesFromString);
    FRIEND_TEST(RestApiTest, EmptyStartingPrefs);
    FRIEND_TEST(RestApiTest, GetExistingPrefs);
    FRIEND_TEST(RestApiTest, DeletePrefsEmpty);
    FRIEND_TEST(RestApiTest, DeletePrefsInvalid);
    FRIEND_TEST(RestApiTest, DeletePrefsIgnoresInvalidKeys);
    FRIEND_TEST(RestApiTest, DeletePrefsHandlesMissingKeys);
    FRIEND_TEST(RestApiTest, DeletePrefsSingleKey);
    FRIEND_TEST(RestApiTest, DeletePrefsKeyList);
    FRIEND_TEST(RestApiTest, EmptyStartingLayouts);
    FRIEND_TEST(RestApiTest, GetExistingLayouts);
    FRIEND_TEST(RestApiTest, DeleteLayout);
    FRIEND_TEST(RestApiTest, DeleteLayoutEmpty);
    FRIEND_TEST(RestApiTest, DeleteLayoutInvalid);
    FRIEND_TEST(RestApiTest, DeleteLayoutIgnoresInvalidKeys);
    FRIEND_TEST(RestApiTest, DeleteLayoutMissingName);
    FRIEND_TEST(RestApiTest, SetLayout);
};

class RestApiTest : public ::testing::Test {
public:
    std::unique_ptr<TestSimpleFrontendServer> _frontend_server;
    fs::path preferences_path;
    fs::path layouts_path;
    json example_options;
    json example_layout;

    RestApiTest() {
        // InitLogger(true, 0, false, false);
        preferences_path = fs::path(getenv("HOME")) / CARTA_USER_FOLDER_PREFIX / "config/preferences.json";
        layouts_path = fs::path(getenv("HOME")) / CARTA_USER_FOLDER_PREFIX / "config/layouts";
        example_options = R"({
            "$schema": "https://cartavis.github.io/schemas/preference_schema_1.json",
            "version": 1,
            "astGridVisible": false,
            "beamColor": "#8A9BA8",
            "beamType": "open",
            "beamVisible": true,
            "beamWidth": 1
        })"_json;

        example_layout = R"({
            "$schema": "https://cartavis.github.io/schemas/layout_schema_2.json",
            "layoutVersion": 2,
                "docked": {
                "type": "stack",
                "content": [{
                    "type": "component",
                        "id": "image-view",
                        "height": 70
                }]
            },
            "floating": []
        })"_json;
    }
    void SetUp() {
        _frontend_server.reset(new TestSimpleFrontendServer("/", "my_test_key"));
        fs::remove(preferences_path);
        fs::remove_all(layouts_path);
    }

    void WriteDefaultPrefs() {
        fs::create_directories(preferences_path.parent_path());
        ofstream(preferences_path.string()) << example_options.dump(4);
    }

    void WriteDefaultLayouts() {
        fs::create_directories(layouts_path);
        ofstream((layouts_path / "test_layout.json").string()) << example_options.dump(4);
        ofstream((layouts_path / "test_layout2.json").string()) << example_options.dump();
        ofstream((layouts_path / "test_layout3.json").string()) << "this is not a json file!";
        ofstream((layouts_path / "bad_layout_name").string()) << example_options.dump(4);
    }

    void TearDown() {
        // Remove .carta-unit-tests/config/preferences (and empty dirs)
        fs::remove(preferences_path);
        fs::remove_all(layouts_path);
        fs::remove(preferences_path.parent_path());
        fs::remove(preferences_path.parent_path().parent_path());
    }
};

TEST_F(RestApiTest, EmptyStartingPrefs) {
    auto existing_preferences = _frontend_server->GetExistingPreferences();
    EXPECT_EQ(existing_preferences, json({{"version", 1}}));
}

TEST_F(RestApiTest, GetExistingPrefs) {
    WriteDefaultPrefs();
    auto existing_preferences = _frontend_server->GetExistingPreferences();
    EXPECT_EQ(existing_preferences, example_options);
}

TEST_F(RestApiTest, DeletePrefsEmpty) {
    WriteDefaultPrefs();
    auto status = _frontend_server->UpdatePreferencesFromString("");
    EXPECT_EQ(status, HTTP_400);
}

TEST_F(RestApiTest, DeletePrefsInvalid) {
    WriteDefaultPrefs();
    auto status = _frontend_server->UpdatePreferencesFromString("this_is_not_a_json_string");
    EXPECT_EQ(status, HTTP_400);
}

TEST_F(RestApiTest, DeletePrefsIgnoresInvalidKeys) {
    WriteDefaultPrefs();
    json keys = {{"keys", {24}}};
    auto status = _frontend_server->ClearPreferencesFromString(keys.dump());
    EXPECT_EQ(status, HTTP_200);
    auto existing_preferences = _frontend_server->GetExistingPreferences();
    EXPECT_EQ(existing_preferences, example_options);
}

TEST_F(RestApiTest, DeletePrefsHandlesMissingKeys) {
    WriteDefaultPrefs();
    json keys = {{"keys", {"thisKeyIsDefinitelyMissing"}}};
    auto status = _frontend_server->ClearPreferencesFromString(keys.dump());
    EXPECT_EQ(status, HTTP_200);
    auto existing_preferences = _frontend_server->GetExistingPreferences();
    EXPECT_EQ(existing_preferences, example_options);
}

TEST_F(RestApiTest, DeletePrefsSingleKey) {
    WriteDefaultPrefs();
    json keys = {{"keys", {"beamType"}}};
    auto status = _frontend_server->ClearPreferencesFromString(keys.dump());
    EXPECT_EQ(status, HTTP_200);
    auto existing_preferences = _frontend_server->GetExistingPreferences();
    EXPECT_TRUE(existing_preferences["beamType"].empty());
    // Check that only the beamType key has been modified
    existing_preferences["beamType"] = "open";
    EXPECT_EQ(existing_preferences, example_options);
}

TEST_F(RestApiTest, DeletePrefsKeyList) {
    WriteDefaultPrefs();
    json keys = {{"keys", {"beamType", "beamColor"}}};
    auto status = _frontend_server->ClearPreferencesFromString(keys.dump());
    EXPECT_EQ(status, HTTP_200);
    auto existing_preferences = _frontend_server->GetExistingPreferences();
    EXPECT_TRUE(existing_preferences["beamType"].empty());
    EXPECT_TRUE(existing_preferences["beamColor"].empty());
    // Check that only the beamType and beamColor keys have been modified
    existing_preferences["beamType"] = "open";
    existing_preferences["beamColor"] = "#8A9BA8";
    EXPECT_EQ(existing_preferences, example_options);
}

TEST_F(RestApiTest, EmptyStartingLayouts) {
    auto existing_layouts = _frontend_server->GetExistingLayouts();
    EXPECT_TRUE(existing_layouts.empty());
}

TEST_F(RestApiTest, GetExistingLayouts) {
    WriteDefaultLayouts();
    auto existing_layouts = _frontend_server->GetExistingLayouts();
    EXPECT_EQ(existing_layouts["test_layout"], example_options);
    EXPECT_EQ(existing_layouts["test_layout2"], example_options);
}

TEST_F(RestApiTest, DeleteLayout) {
    WriteDefaultLayouts();
    json body = {{"layoutName", "test_layout"}};
    auto status = _frontend_server->ClearLayoutFromString(body.dump());
    EXPECT_EQ(status, HTTP_200);
    auto existing_layouts = _frontend_server->GetExistingLayouts();
    EXPECT_TRUE(existing_layouts["test_layout"].is_null());
    EXPECT_EQ(existing_layouts["test_layout2"], example_options);
}

TEST_F(RestApiTest, DeleteLayoutEmpty) {
    WriteDefaultLayouts();
    auto status = _frontend_server->ClearLayoutFromString("");
    EXPECT_EQ(status, HTTP_400);
    auto existing_layouts = _frontend_server->GetExistingLayouts();
    EXPECT_EQ(existing_layouts["test_layout"], example_options);
    EXPECT_EQ(existing_layouts["test_layout2"], example_options);
}

TEST_F(RestApiTest, DeleteLayoutInvalid) {
    WriteDefaultLayouts();
    auto status = _frontend_server->ClearLayoutFromString("this_is_not_a_json_string");
    EXPECT_EQ(status, HTTP_400);
    auto existing_layouts = _frontend_server->GetExistingLayouts();
    EXPECT_EQ(existing_layouts["test_layout"], example_options);
    EXPECT_EQ(existing_layouts["test_layout2"], example_options);
}

TEST_F(RestApiTest, DeleteLayoutIgnoresInvalidKeys) {
    WriteDefaultLayouts();
    json body = {{"another_weird_key", "hello"}};
    auto status = _frontend_server->ClearLayoutFromString(body.dump());
    EXPECT_EQ(status, HTTP_400);
    auto existing_layouts = _frontend_server->GetExistingLayouts();
    EXPECT_EQ(existing_layouts["test_layout"], example_options);
    EXPECT_EQ(existing_layouts["test_layout2"], example_options);
}

TEST_F(RestApiTest, DeleteLayoutMissingName) {
    WriteDefaultLayouts();
    json body = {{"layoutName", "thisLayoutIsDefinitelyMissing"}};
    auto status = _frontend_server->ClearPreferencesFromString(body.dump());
    EXPECT_EQ(status, HTTP_400);
    auto existing_layouts = _frontend_server->GetExistingLayouts();
    EXPECT_EQ(existing_layouts["test_layout"], example_options);
    EXPECT_EQ(existing_layouts["test_layout2"], example_options);
}

TEST_F(RestApiTest, SetLayout) {
    json body = {{"layoutName", "created_layout"}, {"layout", example_layout}};
    auto status = _frontend_server->SetLayoutFromString(body.dump());
    EXPECT_EQ(status, HTTP_200);
    auto existing_layouts = _frontend_server->GetExistingLayouts();
    EXPECT_EQ(existing_layouts["created_layout"], example_layout);
    EXPECT_TRUE(existing_layouts["test_layout2"].is_null());
}