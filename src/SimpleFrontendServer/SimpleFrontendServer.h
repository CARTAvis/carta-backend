/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_SRC_HTTPSERVER_SIMPLEFRONTENDSERVER_H_
#define CARTA_BACKEND_SRC_HTTPSERVER_SIMPLEFRONTENDSERVER_H_

#include <string>

#include <uWebSockets/App.h>
#include <nlohmann/json.hpp>

#include "Util/FileSystem.h"

namespace carta {
#define HTTP_200 "200 OK"
#define HTTP_400 "400 Bad Request"
#define HTTP_404 "404 Not Found"
#define HTTP_403 "403 Forbidden"
#define HTTP_500 "500 Internal Server Error"
#define HTTP_501 "501 Not Implemented"

// Schema URLs
#define CARTA_PREFERENCES_SCHEMA_URL "https://cartavis.github.io/schemas/preference_schema_1.json"
#define CARTA_LAYOUT_SCHEMA_URL "https://cartavis.github.io/schemas/layout_schema_2.json"
#define CARTA_SNIPPET_SCHEMA_URL "https://cartavis.github.io/schemas/snippet_schema_1.json"

typedef uWS::HttpRequest Req;
typedef uWS::HttpResponse<false> Res;

class SimpleFrontendServer {
public:
    SimpleFrontendServer(fs::path root_folder, fs::path user_directory, std::string auth_token, bool read_only_mode);
    bool CanServeFrontend() {
        return _frontend_found;
    }

    void RegisterRoutes(uWS::App& app);
    static std::string GetFileUrlString(std::vector<std::string> files);

protected:
    nlohmann::json GetExistingPreferences();
    std::string_view UpdatePreferencesFromString(const std::string& buffer);
    std::string_view ClearPreferencesFromString(const std::string& buffer);
    nlohmann::json GetExistingObjects(const std::string& object_type);
    std::string_view SetObjectFromString(const std::string& object_type, const std::string& buffer);
    std::string_view ClearObjectFromString(const std::string& object_type, const std::string& buffer);

private:
    static bool IsValidFrontendFolder(fs::path folder);
    bool IsAuthenticated(Req* req);
    void AddNoCacheHeaders(Res* res);

    bool WritePreferencesFile(nlohmann::json& obj);
    bool WriteObjectFile(const std::string& object_type, const std::string& object_name, nlohmann::json& obj);
    void WaitForData(Res* res, Req* req, const std::function<void(const std::string&)>& callback);

    void HandleStaticRequest(Res* res, Req* req);
    void HandleGetConfig(Res* res, Req* req);
    void HandleGetPreferences(Res* res, Req* req);
    void HandleSetPreferences(Res* res, Req* req);
    void HandleClearPreferences(Res* res, Req* req);
    void HandleGetObjects(const std::string& object_type, Res* res, Req* req);
    void HandleSetObject(const std::string& object_type, Res* res, Req* req);
    void HandleClearObject(const std::string& object_type, Res* res, Req* req);

    fs::path _http_root_folder;
    fs::path _config_folder;
    bool _frontend_found;
    std::string _auth_token;
    bool _read_only_mode;
};

} // namespace carta
#endif // CARTA_BACKEND_SRC_HTTPSERVER_SIMPLEFRONTENDSERVER_H_
