/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "SimpleFrontendServer.h"

#include <fstream>
#include <regex>
#include <vector>

#include <curl/curl.h>

#include "Logger/Logger.h"
#include "MimeTypes.h"
#include "Util/Token.h"

using namespace std;
using json = nlohmann::json;

namespace carta {

const string success_string = json({{"success", true}}).dump();

SimpleFrontendServer::SimpleFrontendServer(fs::path root_folder, fs::path user_directory, string auth_token, bool read_only_mode)
    : _http_root_folder(root_folder), _auth_token(auth_token), _read_only_mode(read_only_mode), _config_folder(user_directory / "config") {
    _frontend_found = IsValidFrontendFolder(root_folder);

    if (_frontend_found) {
        spdlog::info("Serving CARTA frontend from {}", fs::canonical(_http_root_folder).string());
    } else {
        spdlog::warn("Could not find CARTA frontend files in directory {}.", _http_root_folder.string());
    }
}

void SimpleFrontendServer::RegisterRoutes(uWS::App& app) {
    // Dynamic routes for preferences, layouts and snippets
    app.get("/api/database/preferences", [&](auto res, auto req) { HandleGetPreferences(res, req); });
    app.put("/api/database/preferences", [&](auto res, auto req) { HandleSetPreferences(res, req); });
    app.del("/api/database/preferences", [&](auto res, auto req) { HandleClearPreferences(res, req); });
    app.get("/api/database/layouts", [&](auto res, auto req) { HandleGetObjects("layout", res, req); });
    app.put("/api/database/layout", [&](auto res, auto req) { HandleSetObject("layout", res, req); });
    app.del("/api/database/layout", [&](auto res, auto req) { HandleClearObject("layout", res, req); });
    app.get("/api/database/snippets", [&](auto res, auto req) { HandleGetObjects("snippet", res, req); });
    app.put("/api/database/snippet", [&](auto res, auto req) { HandleSetObject("snippet", res, req); });
    app.del("/api/database/snippet", [&](auto res, auto req) { HandleClearObject("snippet", res, req); });
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
    return ValidateAuthToken(req, _auth_token);
}

void SimpleFrontendServer::AddNoCacheHeaders(Res* res) {
    res->writeHeader("Cache-Control", "private, no-cache, no-store, must-revalidate");
    res->writeHeader("Expires", "-1");
    res->writeHeader("Pragma", "no-cache");
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
    if (_read_only_mode) {
        spdlog::warn("Writing preferences file is not allowed in read-only mode");
        return false;
    }

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
    res->onData([callback, buffer = std::move(buffer)](std::string_view data, bool last) mutable {
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
    AddNoCacheHeaders(res);

    // Read preferences JSON file
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
    AddNoCacheHeaders(res);

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
    AddNoCacheHeaders(res);

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

void SimpleFrontendServer::HandleGetObjects(const std::string& object_type, Res* res, Req* req) {
    if (!IsAuthenticated(req)) {
        res->writeStatus(HTTP_403)->end();
        return;
    }
    AddNoCacheHeaders(res);

    json existing_objects = GetExistingObjects(object_type);
    res->writeHeader("Content-Type", "application/json");
    json body = {{"success", true}, {(object_type + "s"), existing_objects}};
    res->writeStatus(HTTP_200)->end(body.dump());
}

void SimpleFrontendServer::HandleSetObject(const std::string& object_type, Res* res, Req* req) {
    if (!IsAuthenticated(req)) {
        res->writeStatus(HTTP_403)->end();
        return;
    }
    AddNoCacheHeaders(res);

    WaitForData(res, req, [this, object_type, res](const string& buffer) {
        auto status = SetObjectFromString(object_type, buffer);
        res->writeStatus(status);
        if (status == HTTP_200) {
            res->end(success_string);
        } else {
            res->end();
        }
    });
}

void SimpleFrontendServer::HandleClearObject(const std::string& object_type, Res* res, Req* req) {
    if (!IsAuthenticated(req)) {
        res->writeStatus(HTTP_403)->end();
        return;
    }
    AddNoCacheHeaders(res);

    WaitForData(res, req, [this, object_type, res](const string& buffer) {
        auto status = ClearObjectFromString(object_type, buffer);
        res->writeStatus(status);
        if (status == HTTP_200) {
            res->end(success_string);
        } else {
            res->end();
        }
    });
}

nlohmann::json SimpleFrontendServer::GetExistingObjects(const std::string& object_type) {
    auto object_folder = _config_folder / (object_type + "s");
    json objects = json::object();
    if (fs::exists(object_folder)) {
        for (auto& p : fs::directory_iterator(object_folder)) {
            try {
                string filename = p.path().filename().string();
                regex object_regex(R"(^(.+)\.json$)");
                smatch sm;
                if (fs::is_regular_file(p) && regex_search(filename, sm, object_regex) && sm.size() == 2) {
                    string object_name = sm[1];
                    ifstream file(p.path().string());
                    string json_string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                    json obj = json::parse(json_string);
                    objects[object_name] = obj;
                }
            } catch (exception e) {
                spdlog::warn(e.what());
            }
        }
    }
    return objects;
}

bool SimpleFrontendServer::WriteObjectFile(const std::string& object_type, const string& object_name, nlohmann::json& obj) {
    if (_read_only_mode) {
        spdlog::warn("Writing {} file is not allowed in read-only mode", object_type);
        return false;
    }

    auto object_path = _config_folder / (object_type + "s") / (object_name + ".json");

    try {
        fs::create_directories(object_path.parent_path());
        ofstream file(object_path.string());
        // Ensure correct schema value is written
        if (object_type == "layout") {
            obj["$schema"] = CARTA_LAYOUT_SCHEMA_URL;
        } else if (object_type == "snippet") {
            obj["$schema"] = CARTA_SNIPPET_SCHEMA_URL;
        }

        auto json_string = obj.dump(4);
        file << json_string;
        return true;
    } catch (exception e) {
        spdlog::warn(e.what());
        return false;
    }
}

std::string_view SimpleFrontendServer::SetObjectFromString(const std::string& object_type, const string& buffer) {
    try {
        string field_name = object_type + "Name";
        json post_data = json::parse(buffer);
        if (post_data[field_name].is_string()) {
            string object_name = post_data[field_name];
            auto object_data = post_data[object_type];
            if (!object_name.empty() && object_data.is_object()) {
                return WriteObjectFile(object_type, object_name, object_data) ? HTTP_200 : HTTP_400;
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

std::string_view SimpleFrontendServer::ClearObjectFromString(const std::string& object_type, const string& buffer) {
    if (_read_only_mode) {
        spdlog::warn("Writing {} file is not allowed in read-only mode", object_type);
        return HTTP_400;
    }

    try {
        string field_name = object_type + "Name";
        json post_data = json::parse(buffer);
        if (post_data[field_name].is_string()) {
            string object_name = post_data[field_name];
            if (!object_name.empty()) {
                auto object_path = _config_folder / (object_type + "s") / (object_name + ".json");
                if (fs::exists(object_path) && fs::is_regular_file(object_path)) {
                    fs::remove(object_path);
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

std::string SimpleFrontendServer::GetFileUrlString(vector<std::string> files) {
    if (files.empty()) {
        return std::string();
    } else if (files.size() == 1) {
        return fmt::format("file={}", curl_easy_escape(nullptr, files[0].c_str(), 0));
    } else {
        bool in_common_folder = true;
        fs::path common_folder;
        std::string url_string;
        for (auto& file : files) {
            fs::path p(file);
            auto folder = p.parent_path();
            if (common_folder.empty()) {
                common_folder = folder;
            } else if (folder != common_folder) {
                in_common_folder = false;
                break;
            }
        }

        if (in_common_folder) {
            url_string += fmt::format("folder={}&", curl_easy_escape(nullptr, common_folder.c_str(), 0));
            // Trim folder from path string
            for (auto& file : files) {
                fs::path p(file);
                file = p.filename().string();
            }
        }

        int num_files = files.size();
        url_string += "files=";
        for (int i = 0; i < num_files; i++) {
            url_string += curl_easy_escape(nullptr, files[i].c_str(), 0);
            if (i != num_files - 1) {
                url_string += ",";
            }
        }
        return url_string;
    }
}

} // namespace carta
