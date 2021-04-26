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

#ifdef _BOOST_FILESYSTEM_
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

namespace carta {
#define HTTP_200 "200 OK"
#define HTTP_400 "400 Bad Request"
#define HTTP_404 "404 Not Found"
#define HTTP_403 "403 Forbidden"
#define HTTP_500 "500 Internal Server Error"
#define HTTP_501 "501 Not Implemented"

typedef uWS::HttpRequest Req;
typedef uWS::HttpResponse<false> Res;

class SimpleFrontendServer {
public:
    SimpleFrontendServer(fs::path root_folder, std::string auth_token, bool read_only_mode);
    bool CanServeFrontend() {
        return _frontend_found;
    }

    void RegisterRoutes(uWS::App& app);

protected:
    nlohmann::json GetExistingPreferences();
    std::string_view UpdatePreferencesFromString(const std::string& buffer);
    std::string_view ClearPreferencesFromString(const std::string& buffer);
    nlohmann::json GetExistingLayouts();
    std::string_view SetLayoutFromString(const std::string& buffer);
    std::string_view ClearLayoutFromString(const std::string& buffer);

private:
    static bool IsValidFrontendFolder(fs::path folder);
    bool IsAuthenticated(Req* req);
    void AddNoCacheHeaders(Res* res);

    bool WritePreferencesFile(nlohmann::json& obj);
    bool WriteLayoutFile(const std::string& layout_name, nlohmann::json& obj);
    void WaitForData(Res* res, Req* req, const std::function<void(const std::string&)>& callback);

    void HandleStaticRequest(Res* res, Req* req);
    void HandleGetConfig(Res* res, Req* req);
    void HandleGetPreferences(Res* res, Req* req);
    void HandleSetPreferences(Res* res, Req* req);
    void HandleClearPreferences(Res* res, Req* req);
    void HandleGetLayouts(Res* res, Req* req);
    void HandleSetLayout(Res* res, Req* req);
    void HandleClearLayout(Res* res, Req* req);

    fs::path _http_root_folder;
    fs::path _config_folder;
    bool _frontend_found;
    std::string _auth_token;
    bool _read_only_mode;
};

} // namespace carta
#endif // CARTA_BACKEND_SRC_HTTPSERVER_SIMPLEFRONTENDSERVER_H_
