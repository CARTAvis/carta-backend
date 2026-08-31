/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "HttpServer.h"

#include <fstream>
#include <map>
#include <regex>
#include <vector>

#include "Logger/Logger.h"
#include "MimeTypes.h"
#include "Util/Json.h"
#include "Util/String.h"
#include "Util/Token.h"

#if defined(__APPLE__)
#define st_mtim st_mtimespec
#endif

using json = nlohmann::json;

namespace carta {

const std::string SUCCESS_STRING = json({{"success", true}}).dump();
const std::unordered_set<std::string> OBJECT_TYPES = {"layout", "snippet", "workspace"};

uint32_t HttpServer::_scripting_request_id = 0;

HttpServer::HttpServer(std::shared_ptr<SessionManager> session_manager, fs::path root_folder, fs::path user_directory,
    std::string auth_token, bool read_only_mode, bool enable_frontend, bool enable_database, bool enable_scripting,
    bool enable_runtime_config, std::string url_prefix)
    : _session_manager(session_manager),
      _http_root_folder(root_folder),
      _auth_token(auth_token),
      _read_only_mode(read_only_mode),
      _config_folder(user_directory / "config"),
      _enable_frontend(enable_frontend),
      _enable_database(enable_database),
      _enable_scripting(enable_scripting),
      _enable_runtime_config(enable_runtime_config),
      _url_prefix(url_prefix) {
    if (_enable_frontend && !root_folder.empty()) {
        _frontend_found = IsValidFrontendFolder(root_folder);

        if (_frontend_found) {
            spdlog::info("Serving CARTA frontend from {}", fs::canonical(_http_root_folder).string());
        } else {
            spdlog::warn("Could not find CARTA frontend files in directory {}.", _http_root_folder.string());
        }
    }
}

void HttpServer::RegisterRoutes() {
    uWS::App& app = _session_manager->App();

    if (_enable_scripting) {
        app.post(fmt::format("{}/api/scripting/action", _url_prefix), [&](auto res, auto req) { HandleScriptingAction(res, req); });
    } else {
        app.post(fmt::format("{}/api/scripting/action", _url_prefix), [&](auto res, auto req) { NotImplemented(res, req); });
    }

    if (_enable_database) {
        // Dynamic routes for preferences, layouts, snippets and workspaces
        app.get(fmt::format("{}/api/database/preferences", _url_prefix), [&](auto res, auto req) { HandleGetPreferences(res, req); });
        app.put(fmt::format("{}/api/database/preferences", _url_prefix), [&](auto res, auto req) { HandleSetPreferences(res, req); });
        app.del(fmt::format("{}/api/database/preferences", _url_prefix), [&](auto res, auto req) { HandleClearPreferences(res, req); });

        for (const auto& object_type : OBJECT_TYPES) {
            app.put(fmt::format("{}/api/database/{}", _url_prefix, object_type),
                [&](auto res, auto req) { HandleSetObject(object_type, res, req); });
            app.del(fmt::format("{}/api/database/{}", _url_prefix, object_type),
                [&](auto res, auto req) { HandleClearObject(object_type, res, req); });

            if (object_type == "workspace") {
                app.get(fmt::format("{}/api/database/list/{}s", _url_prefix, object_type),
                    [&](auto res, auto req) { HandleGetObjectList(object_type, res, req); });
                app.get(fmt::format("{}/api/database/{}/:name", _url_prefix, object_type),
                    [&](auto res, auto req) { HandleGetObject(object_type, res, req); });
            } else {
                app.get(fmt::format("{}/api/database/{}s", _url_prefix, object_type),
                    [&](auto res, auto req) { HandleGetObjects(object_type, res, req); });
            }
        }
    } else {
        app.get(fmt::format("{}/api/database/*", _url_prefix), [&](auto res, auto req) { NotImplemented(res, req); });
        app.put(fmt::format("{}/api/database/*", _url_prefix), [&](auto res, auto req) { NotImplemented(res, req); });
        app.del(fmt::format("{}/api/database/*", _url_prefix), [&](auto res, auto req) { NotImplemented(res, req); });
    }

    if (_enable_frontend) {
        if (_enable_runtime_config) {
            app.get(fmt::format("{}/config", _url_prefix), [&](auto res, auto req) { HandleGetConfig(res, req); });
        } else {
            app.get(fmt::format("{}/config", _url_prefix), [&](auto res, auto req) { DefaultSuccess(res, req); });
        }
        // Static routes for all other files
        app.get(fmt::format("{}/*", _url_prefix), [&](Res* res, Req* req) { HandleStaticRequest(res, req); });
    } else {
        app.get(fmt::format("{}/*", _url_prefix), [&](auto res, auto req) { NotImplemented(res, req); });
    }

    // CORS support for the API
    app.options(fmt::format("{}/api/*", _url_prefix), [&](auto res, auto req) {
        AddCorsHeaders(res);
        res->end();
    });
}

void HttpServer::HandleGetConfig(Res* res, Req* req) {
    json runtime_config = {{"apiAddress", fmt::format("{}/api", _url_prefix)}};
    res->writeStatus(HTTP_200);
    res->writeHeader("Content-Type", "application/json");
    res->end(runtime_config.dump());
}

void HttpServer::HandleStaticRequest(Res* res, Req* req) {
    std::string url(req->getUrl());
    fs::path path = _http_root_folder;

    // Trim prefix and any number of slashes before and after it
    std::regex prefix(fmt::format("^/*{}/*", _url_prefix));
    url = std::regex_replace(url, prefix, "");

    if (url.empty()) {
        path /= "index.html";
    } else {
        path /= url;
    }

    std::error_code error_code;
    auto relative_path = fs::relative(path, _http_root_folder, error_code).string();

    // Prevent serving of any files outside the HTTP root folder
    if (error_code || !relative_path.size() || relative_path.find("..") != std::string::npos) {
        res->writeStatus(HTTP_403);
        res->end();
        return;
    }

    // Check if we can serve a gzip-compressed alternative
    auto req_encoding_header = req->getHeader("accept-encoding");
    bool accepts_gzip = req_encoding_header.find("gzip") != std::string_view::npos;
    bool gzip_compressed = false;
    auto gzip_path = path;
    gzip_path += ".gz";
    if (accepts_gzip && fs::exists(gzip_path, error_code) && fs::is_regular_file(gzip_path, error_code)) {
        gzip_compressed = true;
        path = gzip_path;
    }

    if (fs::exists(path, error_code) && fs::is_regular_file(path, error_code)) {
        // Check file size
        std::ifstream file(path.string(), std::ios::binary | std::ios::ate);
        if (!file.good()) {
            res->writeStatus(HTTP_404);
            res->end();
            return;
        }
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<char> buffer(size);
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

            std::string_view sv(buffer.data(), buffer.size());
            res->write(sv);
        } else {
            res->writeStatus(HTTP_500);
        }
    } else {
        res->writeStatus(HTTP_404);
    }
    res->end();
}

bool HttpServer::IsValidFrontendFolder(fs::path folder) {
    std::error_code error_code;

    // Check that the folder exists
    if (!fs::exists(folder, error_code) || !fs::is_directory(folder, error_code)) {
        return false;
    }
    // Check that index.html exists
    folder /= "index.html";
    if (!fs::exists(folder, error_code) || !fs::is_regular_file(folder, error_code)) {
        return false;
    }
    // Check that index.html can be read
    std::ifstream index_file(folder.string());
    return index_file.good();
}

bool HttpServer::IsAuthenticated(uWS::HttpRequest* req) {
    return ValidateAuthToken(req, _auth_token);
}

void HttpServer::AddNoCacheHeaders(Res* res) {
    res->writeHeader("Cache-Control", "private, no-cache, no-store, must-revalidate");
    res->writeHeader("Expires", "-1");
    res->writeHeader("Pragma", "no-cache");
    AddCorsHeaders(res);
}

void HttpServer::AddCorsHeaders(Res* res) {
    res->writeHeader("Access-Control-Allow-Origin", "*");
    res->writeHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    res->writeHeader("Access-Control-Allow-Headers", "origin, content-type, accept, x-requested-with");
    res->writeHeader("Access-Control-Max-Age", "3600");
}

void HttpServer::NormalisePreferences(nlohmann::json& obj) {
    // Ensure correct schema and version values are written
    obj["$schema"] = Json::Schema("preferences")["$id"];
    obj["version"] = 2;
}

bool HttpServer::ValidatePreferences(nlohmann::json& obj) {
    bool valid(true);
    auto error_callback = [&](const json::json_pointer& pointer, const json& instance, const std::string& message) {
        spdlog::debug("Error validating preferences at {} with value {}: {}", pointer.to_string(), instance.dump(), message);
        valid = false;
    };

    JsonCustomErrorHandler error_handler(error_callback);
    Json::Validator("preferences").validate(obj, error_handler);

    return valid;
}

bool HttpServer::ValidateObject(const std::string& object_type, nlohmann::json& obj) {
    bool valid(true);
    auto error_callback = [&](const json::json_pointer& pointer, const json& instance, const std::string& message) {
        spdlog::debug("Error validating {} at {} with value {}: {}", object_type, pointer.to_string(), instance.dump(), message);
        valid = false;
    };

    JsonCustomErrorHandler error_handler(error_callback);
    Json::Validator(object_type).validate(obj, error_handler);

    return valid;
}

void HttpServer::WritePreferencesBackup() {
    auto preferences_path = _config_folder / "preferences.json";
    auto backup_path = _config_folder / "preferences.json.bak";
    std::error_code error_code;

    fs::copy_file(preferences_path, backup_path, fs::copy_options::update_existing, error_code);
    if (error_code) {
        spdlog::warn("Could not back up preferences file: {}", error_code.message());
    } else {
        spdlog::info("Backed up preferences file to {}.", backup_path.string());
    }
}

json HttpServer::GetExistingPreferences() {
    auto preferences_path = _config_folder / "preferences.json";
    json obj = {};

    if (!fs::exists(preferences_path) || !fs::file_size(preferences_path)) {
        return {{"version", 2}};
    }

    std::ifstream file(preferences_path.string());
    std::string json_string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    try {
        obj = json::parse(json_string);
    } catch (json::parse_error e) {
        spdlog::warn("Preferences file is malformed: {}", e.what());
        return {};
    }

    return obj;
}

bool HttpServer::WritePreferencesFile(nlohmann::json& obj) {
    auto preferences_path = _config_folder / "preferences.json";

    // Dump the preferences to string
    std::string json_string;
    try {
        json_string = obj.dump(4);
    } catch (json::type_error e) {
        spdlog::warn(e.what());
        return false;
    }

    // Write out the preferences to file
    fs::create_directories(preferences_path.parent_path().string());
    std::ofstream file(preferences_path.string());
    file << json_string;

    return true;
}

void HttpServer::WaitForData(Res* res, Req* req, const std::function<void(const std::string&)>& callback) {
    res->onAborted([res]() { res->writeStatus(HTTP_500)->end(); });

    std::string buffer;
    // Adapted from https://github.com/uNetworking/uWebSockets/issues/805#issuecomment-452182209
    res->onData([callback, buffer = std::move(buffer)](std::string_view data, bool last) mutable {
        buffer.append(data.data(), data.length());
        if (last) {
            callback(buffer);
        }
    });
}

void HttpServer::HandleGetPreferences(Res* res, Req* req) {
    if (!IsAuthenticated(req)) {
        res->writeStatus(HTTP_403)->end();
        return;
    }

    // Read preferences JSON file
    json existing_preferences = GetExistingPreferences();
    if (!existing_preferences.empty()) {
        if (!ValidatePreferences(existing_preferences)) {
            spdlog::warn("Returning invalid preferences.");
        }

        res->writeStatus(HTTP_200);
        AddNoCacheHeaders(res);
        res->writeHeader("Content-Type", "application/json");
        json body = {{"success", true}, {"preferences", existing_preferences}};
        res->end(body.dump());
    } else {
        res->writeStatus(HTTP_500);
        AddNoCacheHeaders(res);
        res->end();
    }
}

std::string_view HttpServer::UpdatePreferencesFromString(const std::string& buffer) {
    if (_read_only_mode) {
        spdlog::warn("Writing preferences file is not allowed in read-only mode");
        return HTTP_400;
    }

    try {
        json update_data = json::parse(buffer);

        // Validate the new preferences *before* merging with existing preferences
        // First we need to ensure the version is included
        NormalisePreferences(update_data);
        if (!ValidatePreferences(update_data)) {
            spdlog::warn("Rejecting invalid preference update.");
            return HTTP_400;
        }

        json existing_data = GetExistingPreferences();

        // If returned object is completely empty, the prefs are malformed. Back up before writing.
        bool malformed(existing_data.empty());

        // Apply here too to avoid counting these as changed prefs
        NormalisePreferences(existing_data);

        // Update each preference key-value pair, and count changes
        int modified_key_count = 0;
        for (auto& [key, value] : update_data.items()) {
            if (!existing_data.count(key) || existing_data[key] != value) {
                existing_data[key] = value;
                modified_key_count++;
            }
        }

        if (modified_key_count) {
            if (malformed) {
                spdlog::warn(
                    "Preferences file is malformed. All preferences will be reset to defaults before update. Attempting to back up "
                    "preferences file.");
                WritePreferencesBackup();
            }
            if (WritePreferencesFile(existing_data)) {
                spdlog::debug("Updated {} preferences", modified_key_count);
                return HTTP_200;
            } else {
                return HTTP_400;
            }
        } else {
            return HTTP_200;
        }
    } catch (json::exception e) {
        spdlog::warn(e.what());
        return HTTP_400;
    }
}

void HttpServer::HandleSetPreferences(Res* res, Req* req) {
    // Check authentication
    if (!IsAuthenticated(req)) {
        res->writeStatus(HTTP_403)->end();
        return;
    }

    WaitForData(res, req, [this, res](const std::string& buffer) {
        auto status = UpdatePreferencesFromString(buffer);
        res->writeStatus(status);
        res->writeHeader("Content-Type", "application/json");
        AddNoCacheHeaders(res);
        if (status == HTTP_200) {
            res->end(SUCCESS_STRING);
        } else {
            res->end();
        }
    });
}

std::string_view HttpServer::ClearPreferencesFromString(const std::string& buffer) {
    if (_read_only_mode) {
        spdlog::warn("Writing preferences file is not allowed in read-only mode");
        return HTTP_400;
    }

    try {
        json post_data = json::parse(buffer);
        auto keys_array = post_data["keys"];
        if (keys_array.is_array() && keys_array.size()) {
            json existing_data = GetExistingPreferences();
            int modified_key_count = 0;
            if (!existing_data.empty()) {
                for (auto& key : keys_array) {
                    if (key.is_string()) {
                        auto key_string = key.get<std::string>();
                        if (existing_data.count(key_string)) {
                            existing_data.erase(key_string);
                            modified_key_count++;
                        }
                    }
                }
                if (modified_key_count) {
                    NormalisePreferences(existing_data);
                    if (WritePreferencesFile(existing_data)) {
                        spdlog::debug("Cleared {} preferences", modified_key_count);
                        return HTTP_200;
                    } else {
                        return HTTP_400;
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

void HttpServer::HandleClearPreferences(Res* res, Req* req) {
    if (!IsAuthenticated(req)) {
        res->writeStatus(HTTP_403)->end();
        return;
    }

    WaitForData(res, req, [this, res](const std::string& buffer) {
        auto status = ClearPreferencesFromString(buffer);
        res->writeStatus(status);
        AddNoCacheHeaders(res);
        res->writeHeader("Content-Type", "application/json");
        if (status == HTTP_200) {
            res->end(SUCCESS_STRING);
        } else {
            res->end();
        }
    });
}

void HttpServer::HandleGetObjectList(const std::string& object_type, Res* res, Req* req) {
    if (!IsAuthenticated(req)) {
        res->writeStatus(HTTP_403)->end();
        return;
    }

    json existing_objects = GetExistingObjectList(object_type);
    res->writeStatus(HTTP_200);
    AddNoCacheHeaders(res);
    res->writeHeader("Content-Type", "application/json");
    json body = {{"success", true}, {(object_type + "s"), existing_objects}};
    res->end(body.dump());
}

void HttpServer::HandleGetObjects(const std::string& object_type, Res* res, Req* req) {
    if (!IsAuthenticated(req)) {
        res->writeStatus(HTTP_403)->end();
        return;
    }

    json existing_objects = GetExistingObjects(object_type);
    res->writeStatus(HTTP_200);
    AddNoCacheHeaders(res);
    res->writeHeader("Content-Type", "application/json");
    json body = {{"success", true}, {(object_type + "s"), existing_objects}};
    res->end(body.dump());
}

void HttpServer::HandleGetObject(const std::string& object_type, Res* res, Req* req) {
    if (!IsAuthenticated(req)) {
        res->writeStatus(HTTP_403)->end();
        return;
    }

    std::string_view object_name = req->getParameter(0);

    if (object_name.empty()) {
        res->writeStatus(HTTP_404)->end();
        return;
    }
    auto object_name_string = SafeStringUnescape(std::string(object_name));
    json existing_object = GetExistingObject(object_type, object_name_string);
    if (existing_object.empty()) {
        res->writeStatus(HTTP_404)->end();
        return;
    }

    if (!existing_object.contains("name")) {
        existing_object["name"] = object_name_string;
    }

    res->writeStatus(HTTP_200);
    AddNoCacheHeaders(res);
    res->writeHeader("Content-Type", "application/json");
    json body = {{"success", true}, {object_type, existing_object}};
    res->end(body.dump());
}

void HttpServer::HandleSetObject(const std::string& object_type, Res* res, Req* req) {
    if (!IsAuthenticated(req)) {
        res->writeStatus(HTTP_403)->end();
        return;
    }

    WaitForData(res, req, [this, object_type, res](const std::string& buffer) {
        auto status = SetObjectFromString(object_type, buffer);
        res->writeStatus(status);
        AddNoCacheHeaders(res);
        res->writeHeader("Content-Type", "application/json");
        if (status == HTTP_200) {
            res->end(SUCCESS_STRING);
        } else {
            res->end();
        }
    });
}

void HttpServer::HandleClearObject(const std::string& object_type, Res* res, Req* req) {
    if (!IsAuthenticated(req)) {
        res->writeStatus(HTTP_403)->end();
        return;
    }

    WaitForData(res, req, [this, object_type, res](const std::string& buffer) {
        auto status = ClearObjectFromString(object_type, buffer);
        res->writeStatus(status);
        AddNoCacheHeaders(res);
        res->writeHeader("Content-Type", "application/json");
        if (status == HTTP_200) {
            res->end(SUCCESS_STRING);
        } else {
            res->end();
        }
    });
}

nlohmann::json HttpServer::GetExistingObjectList(const std::string& object_type) {
    auto object_folder = _config_folder / (object_type + "s");
    std::map<std::string, json> ordered_array;
    std::error_code error_code;

    if (fs::exists(object_folder, error_code)) {
        for (auto& p : fs::directory_iterator(object_folder)) {
            try {
                std::string filename = p.path().filename().string();
                std::regex object_regex(R"((.+)\.json)");
                std::smatch sm;
                if (fs::is_regular_file(p, error_code) && regex_match(filename, sm, object_regex)) {
                    std::string object_name = sm[1];
                    // Get modified date and fill JSON object
                    struct stat file_stats;
                    stat(p.path().c_str(), &file_stats);
                    json object = json::object();
                    object["name"] = object_name;
                    object["date"] = file_stats.st_mtim.tv_sec;
                    ordered_array[object_name] = object;
                }
            } catch (json::exception e) {
                spdlog::warn(e.what());
            }
        }
    }

    json list = json::array();
    for (auto const& [name, entry] : ordered_array) {
        list.push_back(entry);
    }
    return list;
}

nlohmann::json HttpServer::GetObjectFromPath(const fs::path& path, const std::string& object_type) {
    json obj = {};
    std::ifstream file(path);
    std::string json_string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    try {
        obj = json::parse(json_string);
    } catch (json::exception e) {
        std::string type_str(object_type);
        type_str[0] = std::toupper(type_str[0]);
        spdlog::warn("{} file {} is malformed: {}", type_str, path.string(), e.what());
        return obj;
    }

    if (!ValidateObject(object_type, obj)) {
        spdlog::warn("Returning invalid {}.", object_type);
    }

    return obj;
}

nlohmann::json HttpServer::GetExistingObject(const std::string& object_type, const std::string& object_name) {
    auto object_path = _config_folder / (object_type + "s") / (object_name + ".json");
    json obj = {};
    std::error_code error_code;

    if (fs::is_regular_file(object_path, error_code)) {
        obj = GetObjectFromPath(object_path, object_type);
    }

    return obj;
}

nlohmann::json HttpServer::GetExistingObjects(const std::string& object_type) {
    auto object_folder = _config_folder / (object_type + "s");
    json objects = json::object();
    std::error_code error_code;

    if (fs::exists(object_folder, error_code)) {
        for (auto& p : fs::directory_iterator(object_folder)) {
            std::string filename = p.path().filename().string();
            std::regex object_regex(R"((.+)\.json)");
            std::smatch sm;
            if (fs::is_regular_file(p, error_code) && regex_match(filename, sm, object_regex)) {
                auto obj = GetObjectFromPath(p.path(), object_type);
                if (!obj.empty()) {
                    std::string object_name = sm[1];
                    objects[object_name] = obj;
                }
            }
        }
    }
    return objects;
}

bool HttpServer::WriteObjectFile(const std::string& object_type, const std::string& object_name, nlohmann::json& obj) {
    auto object_path = _config_folder / (object_type + "s") / (object_name + ".json");

    // Ensure correct schema value is written
    if (OBJECT_TYPES.count(object_type)) {
        obj["$schema"] = Json::Schema(object_type)["$id"];
    } else {
        spdlog::error("Unknown object types: {}.", object_type);
        return false;
    }

    // Validate the object
    if (!ValidateObject(object_type, obj)) {
        spdlog::warn("Rejecting invalid {} update.", object_type);
        return false;
    }

    // Write out the object to file
    std::string json_string;
    try {
        json_string = obj.dump(4);
    } catch (json::type_error e) {
        spdlog::warn(e.what());
        return false;
    }
    fs::create_directories(object_path.parent_path());
    std::ofstream file(object_path.string());
    file << json_string;

    return true;
}

std::string_view HttpServer::SetObjectFromString(const std::string& object_type, const std::string& buffer) {
    if (_read_only_mode) {
        spdlog::warn("Writing {} file is not allowed in read-only mode", object_type);
        return HTTP_400;
    }

    try {
        std::string field_name = object_type + "Name";
        json post_data = json::parse(buffer);
        if (post_data[field_name].is_string()) {
            std::string object_name = post_data[field_name];
            auto object_data = post_data[object_type];
            if (!object_name.empty() && object_data.is_object()) {
                return WriteObjectFile(object_type, object_name, object_data) ? HTTP_200 : HTTP_400;
            }
        }
        return HTTP_400;
    } catch (json::parse_error e) {
        spdlog::warn(e.what());
        return HTTP_400;
    } catch (std::exception e) {
        spdlog::warn(e.what());
        return HTTP_500;
    }
}

std::string_view HttpServer::ClearObjectFromString(const std::string& object_type, const std::string& buffer) {
    if (_read_only_mode) {
        spdlog::warn("Writing {} file is not allowed in read-only mode", object_type);
        return HTTP_400;
    }

    try {
        std::string field_name = object_type + "Name";
        json post_data = json::parse(buffer);
        if (post_data[field_name].is_string()) {
            std::string object_name = post_data[field_name];
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
    } catch (std::exception e) {
        spdlog::warn(e.what());
        return HTTP_500;
    }
}

std::string HttpServer::GetFileUrlString(std::vector<std::string> files) {
    if (files.empty()) {
        return std::string();
    } else if (files.size() == 1) {
        return fmt::format("file={}", SafeStringEscape(files[0]));
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
            url_string += fmt::format("folder={}&", SafeStringEscape(common_folder));
            // Trim folder from path string
            for (auto& file : files) {
                fs::path p(file);
                file = p.filename().string();
            }
        }

        int num_files = files.size();
        url_string += "files=";
        for (int i = 0; i < num_files; i++) {
            url_string += SafeStringEscape(files[i]);
            if (i != num_files - 1) {
                url_string += ",";
            }
        }
        return url_string;
    }
}

void HttpServer::HandleScriptingAction(Res* res, Req* req) {
    if (!IsAuthenticated(req)) {
        res->writeStatus(HTTP_403)->end();
        return;
    }

    WaitForData(res, req, [this, res](const string& buffer) {
        int session_id;

        ScriptingResponseCallback callback = [this, res](const bool& success, const std::string& message, const std::string& response) {
            std::string response_buffer;
            auto status = OnScriptingResponse(response_buffer, success, message, response);

            res->writeStatus(status);
            AddNoCacheHeaders(res);
            res->writeHeader("Content-Type", "application/json");
            if (status == HTTP_200) {
                res->end(response_buffer);
            } else {
                res->end();
            }
        };

        ScriptingSessionClosedCallback session_closed_callback = [res]() { res->writeStatus(HTTP_404)->end(); };

        ScriptingRequestHandler request_handler = [this](int& session_id, uint32_t& scripting_request_id, std::string& target,
                                                      std::string& action, std::string& parameters, bool& async, std::string& return_path,
                                                      ScriptingResponseCallback callback,
                                                      ScriptingSessionClosedCallback session_closed_callback) {
            return _session_manager->SendScriptingRequest(
                session_id, scripting_request_id, target, action, parameters, async, return_path, callback, session_closed_callback);
        };

        auto status = SendScriptingRequest(buffer, session_id, callback, session_closed_callback, request_handler);

        if (status != HTTP_200) {
            res->writeStatus(status);
            AddNoCacheHeaders(res);
            res->end();
            return;
        }

        res->onAborted([this, session_id, res]() {
            OnScriptingAbort(session_id, _scripting_request_id);
            res->writeStatus(HTTP_500)->end();
        });
    });
}

std::string_view HttpServer::SendScriptingRequest(const std::string& buffer, int& session_id, ScriptingResponseCallback callback,
    ScriptingSessionClosedCallback session_closed_callback, ScriptingRequestHandler request_handler) {
    try {
        json req = json::parse(buffer);

        _scripting_request_id++;
        _scripting_request_id = std::max(_scripting_request_id, 1u);

        session_id = req["session_id"].get<int>();
        std::string target = req["path"].get<std::string>();
        std::string action = req["action"].get<std::string>();
        std::string parameters = req["parameters"].dump();
        bool async = req["async"].get<bool>();

        std::string return_path;
        if (req.contains("return_path")) {
            const auto& return_path_value = req["return_path"];
            if (return_path_value.is_string()) {
                return_path = return_path_value.get<std::string>();
            } else {
                return_path = return_path_value.dump();
            }
        }

        if (!request_handler(
                session_id, _scripting_request_id, target, action, parameters, async, return_path, callback, session_closed_callback)) {
            return HTTP_404;
        }

        return HTTP_200;

    } catch (json::exception e) {
        spdlog::warn(e.what());
        return HTTP_400;
    } catch (std::exception e) {
        spdlog::warn(e.what());
        return HTTP_500;
    }
}

std::string_view HttpServer::OnScriptingResponse(
    std::string& response_buffer, const bool& success, const std::string& message, const std::string& response) {
    json response_obj;

    response_obj["success"] = success;

    if (!message.empty()) {
        response_obj["message"] = message;
    }

    if (!response.empty()) {
        try {
            response_obj["response"] = json::parse(response);
        } catch (json::exception e) {
            spdlog::warn(e.what());
            return HTTP_500;
        }
    }

    response_buffer = response_obj.dump();
    return HTTP_200;
}

void HttpServer::OnScriptingAbort(int session_id, uint32_t scripting_request_id) {
    _session_manager->OnScriptingAbort(session_id, scripting_request_id);
}

void HttpServer::NotImplemented(Res* res, Req* req) {
    res->writeStatus(HTTP_501)->end();
    return;
}

void HttpServer::DefaultSuccess(Res* res, Req* req) {
    res->writeStatus(HTTP_200)->end();
    return;
}

} // namespace carta
