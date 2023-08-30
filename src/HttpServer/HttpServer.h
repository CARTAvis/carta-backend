/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_SRC_HTTPSERVER_HTTPSERVER_H_
#define CARTA_BACKEND_SRC_HTTPSERVER_HTTPSERVER_H_

#include <chrono>
#include <string>

#include <uWebSockets/App.h>
#include <nlohmann/json.hpp>

#include "Session/SessionManager.h"
#include "Util/FileSystem.h"

namespace carta {
#define HTTP_200 "200 OK"
#define HTTP_400 "400 Bad Request"
#define HTTP_404 "404 Not Found"
#define HTTP_403 "403 Forbidden"
#define HTTP_500 "500 Internal Server Error"
#define HTTP_501 "501 Not Implemented"

// Schema URLs
#define CARTA_PREFERENCES_SCHEMA_URL "https://cartavis.github.io/schemas/preferences_schema_2.json"
#define CARTA_LAYOUT_SCHEMA_URL "https://cartavis.github.io/schemas/layout_schema_2.json"
#define CARTA_SNIPPET_SCHEMA_URL "https://cartavis.github.io/schemas/snippet_schema_1.json"
#define CARTA_WORKSPACE_SCHEMA_URL "https://cartavis.github.io/schemas/workspace_schema_1.json"

typedef uWS::HttpRequest Req;
typedef uWS::HttpResponse<false> Res;
typedef std::function<bool(int&, uint32_t&, std::string&, std::string&, std::string&, bool&, std::string&, ScriptingResponseCallback,
    ScriptingSessionClosedCallback)>
    ScriptingRequestHandler;

class HttpServer {
public:
    HttpServer(std::shared_ptr<SessionManager> session_manager, fs::path root_folder, fs::path user_directory, std::string auth_token,
        bool read_only_mode = false, bool enable_frontend = true, bool enable_database = true, bool enable_scripting = false,
        bool enable_runtime_config = true, std::string url_prefix = "");
    bool CanServeFrontend() {
        return _frontend_found;
    }

    void RegisterRoutes();
    static std::string GetFileUrlString(std::vector<std::string> files);

protected:
    nlohmann::json GetExistingPreferences();
    std::string_view UpdatePreferencesFromString(const std::string& buffer);
    std::string_view ClearPreferencesFromString(const std::string& buffer);
    nlohmann::json GetExistingObjectList(const std::string& object_type);
    nlohmann::json GetExistingObjects(const std::string& object_type);
    nlohmann::json GetExistingObject(const std::string& object_type, const std::string& object_name);
    std::string_view SetObjectFromString(const std::string& object_type, const std::string& buffer);
    std::string_view ClearObjectFromString(const std::string& object_type, const std::string& buffer);
    std::string_view SendScriptingRequest(const std::string& buffer, int& session_id, ScriptingResponseCallback callback,
        ScriptingSessionClosedCallback session_closed_callback, ScriptingRequestHandler request_handler);
    std::string_view OnScriptingResponse(
        std::string& response_buffer, const bool& success, const std::string& message, const std::string& response);
    void OnScriptingAbort(int session_id, uint32_t scripting_request_id);

private:
    static bool IsValidFrontendFolder(fs::path folder);
    bool IsAuthenticated(Req* req);
    void AddNoCacheHeaders(Res* res);
    void AddCorsHeaders(Res* res);

    bool WritePreferencesFile(nlohmann::json& obj);
    bool WriteObjectFile(const std::string& object_type, const std::string& object_name, nlohmann::json& obj);
    void WaitForData(Res* res, Req* req, const std::function<void(const std::string&)>& callback);

    void HandleStaticRequest(Res* res, Req* req);
    void HandleGetConfig(Res* res, Req* req);
    void HandleGetPreferences(Res* res, Req* req);
    void HandleSetPreferences(Res* res, Req* req);
    void HandleClearPreferences(Res* res, Req* req);
    void HandleGetObjectList(const std::string& object_type, Res* res, Req* req);
    void HandleGetObject(const std::string& object_type, Res* res, Req* req);
    void HandleGetObjects(const std::string& object_type, Res* res, Req* req);
    void HandleSetObject(const std::string& object_type, Res* res, Req* req);
    void HandleClearObject(const std::string& object_type, Res* res, Req* req);
    void HandleScriptingAction(Res* res, Req* req);
    void NotImplemented(Res* res, Req* req);
    void DefaultSuccess(Res* res, Req* req);

    fs::path _http_root_folder;
    fs::path _config_folder;
    bool _frontend_found;
    std::string _auth_token;
    bool _read_only_mode;
    bool _enable_frontend;
    bool _enable_database;
    bool _enable_scripting;
    bool _enable_runtime_config;
    std::string _url_prefix;
    std::shared_ptr<SessionManager> _session_manager;
    static uint32_t _scripting_request_id;
};

} // namespace carta
#endif // CARTA_BACKEND_SRC_HTTPSERVER_HTTPSERVER_H_
