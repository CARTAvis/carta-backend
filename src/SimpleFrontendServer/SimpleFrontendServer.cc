/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "SimpleFrontendServer.h"

#include <fstream>
#include <vector>

#include "Logger/Logger.h"
#include "MimeTypes.h"

using namespace std;

namespace carta {
SimpleFrontendServer::SimpleFrontendServer(fs::path root_folder) {
    _http_root_folder = root_folder;
    _frontend_found = IsValidFrontendFolder(root_folder);

    if (_frontend_found) {
        spdlog::info("Serving CARTA frontend from {}", fs::canonical(_http_root_folder).string());
    } else {
        spdlog::warn("Could not find CARTA frontend files in directory {}.", _http_root_folder.string());
    }
}

void SimpleFrontendServer::HandleRequest(uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
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

} // namespace carta