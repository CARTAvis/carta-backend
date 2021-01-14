/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "SimpleFrontendServer.h"

#include <filesystem>
#include <fstream>
#include <vector>

#include <fmt/format.h>

using namespace std;

SimpleFrontendServer::SimpleFrontendServer(string root_folder) {
    // If no folder is provided, check if /usr/local/share/carta/frontend exists.
    // If this does not exist, try /usr/share/carta/frontend.
    if (root_folder.empty()) {
        _http_root_folder = "/usr/local/share/carta/frontend";
        if (!IsValidFrontendFolder(_http_root_folder)) {
            _http_root_folder = "/usr/local/carta/frontend";
            _frontend_found = IsValidFrontendFolder(_http_root_folder);
        } else {
            _frontend_found = true;
        }
    } else {
        _http_root_folder = root_folder;
        _frontend_found = IsValidFrontendFolder(root_folder);
    }

    if (_frontend_found) {
        fmt::print("Serving CARTA frontend from {}\n", _http_root_folder);
    } else {
        fmt::print("Could not find CARTA frontend files.\n");
    }
}

void SimpleFrontendServer::HandleRequest(uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
    string_view url = req->getUrl();
    string path_string = _http_root_folder;
    if (url.empty() || url == "/") {
        path_string.append("/index.html");
    } else {
        path_string.append(url);
    }

    filesystem::path path(path_string);
    if (filesystem::exists(path) && filesystem::is_regular_file(path)) {
        // Check file size
        ifstream file(path, ios::binary | ios::ate);
        streamsize size = file.tellg();
        file.seekg(0, ios::beg);

        vector<char> buffer(size);
        if (size && file.read(buffer.data(), size)) {
            res->writeStatus(HTTP_200);
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

bool SimpleFrontendServer::IsValidFrontendFolder(std::string folder) {
    filesystem::path path(folder);
    // Check that the folder exists
    if (!filesystem::exists(path) || !filesystem::is_directory(path)) {
        return false;
    }
    // Check that index.html exists
    path/= "index.html";
    if (!filesystem::exists(path) || !filesystem::is_regular_file(path)) {
        return false;
    }
    // Check that index.html can be read
    ifstream index_file(path);
    return index_file.good();
}
