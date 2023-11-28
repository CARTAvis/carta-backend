/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_HTTPSERVER_MIMETYPES_H_
#define CARTA_SRC_HTTPSERVER_MIMETYPES_H_

#include <string>
#include <unordered_map>

namespace carta {
const static std::unordered_map<std::string, std::string> MimeTypes = {{".css", "text/css"}, {".htm", "text/html"}, {".html", "text/html"},
    {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"}, {".js", "text/javascript"}, {".json", "application/json"}, {".png", "image/png"},
    {".svg", "image/svg+xml"}, {".woff", "font/woff"}, {".woff2", "font/woff2"}, {".wasm", "application/wasm"}};
} // namespace carta

#endif // CARTA_SRC_HTTPSERVER_MIMETYPES_H_
