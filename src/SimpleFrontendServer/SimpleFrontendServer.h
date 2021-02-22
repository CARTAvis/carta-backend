/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_SRC_HTTPSERVER_SIMPLEFRONTENDSERVER_H_
#define CARTA_BACKEND_SRC_HTTPSERVER_SIMPLEFRONTENDSERVER_H_

#include <string>

#include <uWebSockets/App.h>

#ifdef _BOOST_FILESYSTEM_
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

namespace carta {
#define HTTP_200 "200 OK"
#define HTTP_404 "404 Not Found"
#define HTTP_500 "500 Internal Server Error"

class SimpleFrontendServer {
public:
    SimpleFrontendServer(fs::path root_folder);
    bool CanServeFrontend() {
        return _frontend_found;
    }
    void HandleRequest(uWS::HttpResponse<false>* res, uWS::HttpRequest* req);

private:
    static bool IsValidFrontendFolder(fs::path folder);
    fs::path _http_root_folder;
    bool _frontend_found;
};

} // namespace carta
#endif // CARTA_BACKEND_SRC_HTTPSERVER_SIMPLEFRONTENDSERVER_H_
