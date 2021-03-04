/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "SimpleFrontendServer.h"

#include <fstream>
#include <regex>
#include <vector>

#include "Constants.h"
#include "Logger/Logger.h"
#include "MimeTypes.h"
#include "Util.h"

using namespace std;
using json = nlohmann::json;

namespace carta {

const string success_string = json({{"success", true}}).dump();

SimpleFrontendServer::SimpleFrontendServer(fs::path root_folder, string auth_token)
    : _http_root_folder(root_folder), _auth_token(auth_token) {
    _frontend_found = IsValidFrontendFolder(root_folder);

    if (_frontend_found) {
        spdlog::info("Serving CARTA frontend from {}", fs::canonical(_http_root_folder).string());
    } else {
        spdlog::warn("Could not find CARTA frontend files in directory {}.", _http_root_folder.string());
    }

    _config_folder = fs::path(getenv("HOME")) / CARTA_USER_FOLDER_PREFIX / "config";
}

void SimpleFrontendServer::RegisterRoutes(uWS::App& app) {
    // Dynamic routes for preferences and layouts
    app.get("/api/database/preferences", [&](auto res, auto req) { HandleGetPreferences(res, req); });
    app.put("/api/database/preferences", [&](auto res, auto req) { HandleSetPreferences(res, req); });
    app.del("/api/database/preferences", [&](auto res, auto req) { HandleClearPreferences(res, req); });
    app.get("/api/database/layouts", [&](auto res, auto req) { HandleGetLayouts(res, req); });
    app.put("/api/database/layout", [&](auto res, auto req) { HandleSetLayout(res, req); });
    app.del("/api/database/layout", [&](auto res, auto req) { HandleClearLayout(res, req); });
    app.get("/config", [&](auto res, auto req) { HandleGetConfig(res, req); });

    // Static routes for all other files
    app.get("/*", [&](Res* res, Req* req) { HandleStaticRequest(res, req); });
}

void SimpleFrontendServer::HandleGetConfig(Res* res, Req* _req) {
    json runtime_config = {{"apiAddress", "/api"}};
    res->writeHeader("Content-Type", "application/json");
    res->writeStatus(HTTP_200)->end(runtime_config.dump());
}

void SimpleFrontendServer::HandleStaticRequest(Res* res, Req* req) {
    string_view url = req->getUrl();
    fs::path path = _http_root_folder;
    if (url.empty() || url == "/") {
        path /= "index.html";
    } else {
        // Trim leading '/'
        if (url[0] == '/') {
            url = url.substr(1);
        }
        path /= string(url);
    }

    // Check if we can serve a gzip-compressed alternative
    auto req_encoding_header = req->getHeader("accept-encoding");
    bool accepts_gzip = req_encoding_header.find("gzip") != string_view::npos;
    bool gzip_compressed = false;
    auto gzip_path = path;
    gzip_path += ".gz";
    if (accepts_gzip && fs::exists(gzip_path) && fs::is_regular_file(gzip_path)) {
        gzip_compressed = true;
        path = gzip_path;
    }

    if (fs::exists(path) && fs::is_regular_file(path)) {
        // Check file size
        ifstream file(path.string(), ios::binary | ios::ate);
        streamsize size = file.tellg();
        file.seekg(0, ios::beg);

        vector<char> buffer(size);
        if (size && file.read(buffer.data(), size)) {
            res->writeStatus(HTTP_200);

            if (gzip_compressed) {
                res->writeHeader("Content-Encoding", "gzip");
            }
            auto extension = path.extension();
            auto it = MimeTypes.find(extension.string());
            if (it != MimeTypes.end()) {
                auto val = it->second;
                res->writeHeader("Content-Type", it->second);
            }

            string_view sv(buffer.data(), buffer.size());
            res->write(sv);
        } else {
            res->writeStatus(HTTP_500);
        }
    } else {
        res->writeStatus(HTTP_404);
    }
    res->end();
}

bool SimpleFrontendServer::IsValidFrontendFolder(fs::path folder) {
    // Check that the folder exists
    if (!fs::exists(folder) || !fs::is_directory(folder)) {
        return false;
    }
    // Check that index.html exists
    folder /= "index.html";
    if (!fs::exists(folder) || !fs::is_regular_file(folder)) {
        return false;
    }
    // Check that index.html can be read
    ifstream index_file(folder.string());
    return index_file.good();
}

bool SimpleFrontendServer::IsAuthenticated(uWS::HttpRequest* req) {
    // Always allow if the auth token is empty
    if (_auth_token.empty()) {
        return true;
    }

    return _auth_token == GetAuthToken(req);
}

json SimpleFrontendServer::GetExistingPreferences() {
    auto preferences_path = _config_folder / "preferences.json";
    if (!fs::exists(preferences_path)) {
        return {{"version", 1}};
    }

    try {
        ifstream file(preferences_path.string());
        string json_string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return json::parse(json_string);
    } catch (exception) {
        return {};
    }
}

bool SimpleFrontendServer::WritePreferencesFile(nlohmann::json& obj) {
    auto preferences_path = _config_folder / "preferences.json";

    try {
        fs::create_directories(preferences_path.parent_path().string());
        ofstream file(preferences_path.string());
        // Ensure correct schema and version values are written
        obj["$schema"] = CARTA_PREFERENCES_SCHEMA_URL;
        obj["version"] = 1;
        auto json_string = obj.dump(4);
        file << json_string;
        return true;
    } catch (exception e) {
        spdlog::warn(e.what());
        return false;
    }
}

void SimpleFrontendServer::WaitForData(Res* res, Req* req, const std::function<void(const string&)>& callback) {
    res->onAborted([res]() { res->writeStatus(HTTP_500)->end(); });

    string buffer;
    // Adapted from https://github.com/uNetworking/uWebSockets/issues/805#issuecomment-452182209
    res->onData([res, req, callback, buffer = std::move(buffer)](std::string_view data, bool last) mutable {
        buffer.append(data.data(), data.length());
        if (last) {
            callback(buffer);
        }
    });
}

void SimpleFrontendServer::HandleGetPreferences(Res* res, Req* req) {
    if (!IsAuthenticated(req)) {
        res->writeStatus(HTTP_403)->end();
        return;
    }

    // Read layout JSON file
    json existing_preferences = GetExistingPreferences();
    if (!existing_preferences.empty()) {
        res->writeHeader("Content-Type", "application/json");
        json body = {{"success", true}, {"preferences", existing_preferences}};
        res->writeStatus(HTTP_200)->end(body.dump());
    } else {
        res->writeStatus(HTTP_500)->end();
    }
}

std::string_view SimpleFrontendServer::UpdatePreferencesFromString(const string& buffer) {
    try {
        json update_data = json::parse(buffer);
        json existing_data = GetExistingPreferences();

        // Update each preference key-value pair
        int modified_key_count = 0;
        for (auto& [key, value] : update_data.items()) {
            existing_data[key] = value;
            modified_key_count++;
        }

        if (modified_key_count) {
            spdlog::debug("Updated {} preferences", modified_key_count);
            if (WritePreferencesFile(existing_data)) {
                return HTTP_200;
            } else {
                return HTTP_500;
            }
        } else {
            return HTTP_200;
        }
    } catch (json::exception e) {
        spdlog::warn(e.what());
        return HTTP_400;
    }
}

void SimpleFrontendServer::HandleSetPreferences(Res* res, Req* req) {
    // Check authentication
    if (!IsAuthenticated(req)) {
        res->writeStatus(HTTP_403)->end();
        return;
    }

    WaitForData(res, req, [this, res](const string& buffer) {
        auto status = UpdatePreferencesFromString(buffer);
        res->writeStatus(status);
        if (status == HTTP_200) {
            res->end(success_string);
        } else {
            res->end();
        }
    });
}

std::string_view SimpleFrontendServer::ClearPreferencesFromString(const string& buffer) {
    try {
        json post_data = json::parse(buffer);
        auto keys_array = post_data["keys"];
        if (keys_array.is_array() && keys_array.size()) {
            json existing_data = GetExistingPreferences();
            int modified_key_count = 0;
            if (!existing_data.empty()) {
                for (auto& key : keys_array) {
                    if (key.is_string()) {
                        auto key_string = key.get<string>();
                        if (existing_data.count(key_string)) {
                            existing_data.erase(key_string);
                            modified_key_count++;
                        }
                    }
                }
                if (modified_key_count) {
                    spdlog::debug("Cleared {} preferences", modified_key_count);
                    if (WritePreferencesFile(existing_data)) {
                        return HTTP_200;
                    }
                } else {
                    return HTTP_200;
                }
            }
        } else {
            return HTTP_400;
        }
        return HTTP_500;
    } catch (json::exception e) {
        spdlog::warn(e.what());
        return HTTP_400;
    }
}

void SimpleFrontendServer::HandleClearPreferences(Res* res, Req* req) {
    if (!IsAuthenticated(req)) {
        res->writeStatus(HTTP_403)->end();
        return;
    }

    WaitForData(res, req, [this, res](const string& buffer) {
        auto status = ClearPreferencesFromString(buffer);
        res->writeStatus(status);
        if (status == HTTP_200) {
            res->end(success_string);
        } else {
            res->end();
        }
    });
}

void SimpleFrontendServer::HandleGetLayouts(Res* res, Req* req) {
    if (!IsAuthenticated(req)) {
        res->writeStatus(HTTP_403)->end();
        return;
    }

    json existing_layouts = GetExistingLayouts();
    res->writeHeader("Content-Type", "application/json");
    json body = {{"success", true}, {"layouts", existing_layouts}};
    res->writeStatus(HTTP_200)->end(body.dump());
}

void SimpleFrontendServer::HandleSetLayout(Res* res, Req* req) {
    if (!IsAuthenticated(req)) {
        res->writeStatus(HTTP_403)->end();
        return;
    }

    WaitForData(res, req, [this, res](const string& buffer) {
        auto status = SetLayoutFromString(buffer);
        res->writeStatus(status);
        if (status == HTTP_200) {
            res->end(success_string);
        } else {
            res->end();
        }
    });
}

void SimpleFrontendServer::HandleClearLayout(Res* res, Req* req) {
    if (!IsAuthenticated(req)) {
        res->writeStatus(HTTP_403)->end();
        return;
    }

    WaitForData(res, req, [this, res](const string& buffer) {
        auto status = ClearLayoutFromString(buffer);
        res->writeStatus(status);
        if (status == HTTP_200) {
            res->end(success_string);
        } else {
            res->end();
        }
    });
}

nlohmann::json SimpleFrontendServer::GetExistingLayouts() {
    auto layout_folder = _config_folder / "layouts";
    json layouts = json::object();
    if (fs::exists(layout_folder)) {
        for (auto& p : fs::directory_iterator(layout_folder)) {
            try {
                string filename = p.path().filename().string();
                regex layout_regex(R"(^(.+)\.json$)");
                smatch sm;
                if (fs::is_regular_file(p) && regex_search(filename, sm, layout_regex) && sm.size() == 2) {
                    string layout_name = sm[1];
                    ifstream file(p.path().string());
                    string json_string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                    json layout = json::parse(json_string);
                    layouts[layout_name] = layout;
                }
            } catch (exception e) {
                spdlog::warn(e.what());
            }
        }
    }
    return layouts;
}

bool SimpleFrontendServer::WriteLayoutFile(const string& layout_name, nlohmann::json& obj) {
    auto layout_path = _config_folder / "layouts" / (layout_name + ".json");

    try {
        fs::create_directories(layout_path.parent_path());
        ofstream file(layout_path.string());
        // Ensure correct schema value is written
        obj["$schema"] = CARTA_LAYOUT_SCHEMA_URL;
        auto json_string = obj.dump(4);
        file << json_string;
        return true;
    } catch (exception e) {
        spdlog::warn(e.what());
        return false;
    }
}

std::string_view SimpleFrontendServer::SetLayoutFromString(const string& buffer) {
    try {
        json post_data = json::parse(buffer);
        if (post_data["layoutName"].is_string()) {
            string layout_name = post_data["layoutName"];
            auto layout = post_data["layout"];
            if (!layout_name.empty() && layout.is_object()) {
                return WriteLayoutFile(layout_name, layout) ? HTTP_200 : HTTP_400;
            }
        }
        return HTTP_400;
    } catch (json::exception e) {
        spdlog::warn(e.what());
        return HTTP_400;
    } catch (exception e) {
        spdlog::warn(e.what());
        return HTTP_500;
    }
}

std::string_view SimpleFrontendServer::ClearLayoutFromString(const string& buffer) {
    try {
        json post_data = json::parse(buffer);
        if (post_data["layoutName"].is_string()) {
            string layout_name = post_data["layoutName"];
            if (!layout_name.empty()) {
                auto layout_path = _config_folder / "layouts" / (layout_name + ".json");
                if (fs::exists(layout_path) && fs::is_regular_file(layout_path)) {
                    fs::remove(layout_path);
                    return HTTP_200;
                }
            }
        }
        return HTTP_400;
    } catch (json::exception e) {
        spdlog::warn(e.what());
        return HTTP_400;
    } catch (exception e) {
        spdlog::warn(e.what());
        return HTTP_500;
    }
}

} // namespace carta