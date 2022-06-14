/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018 - 2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__UTIL_TOKEN_H_
#define CARTA_BACKEND__UTIL_TOKEN_H_

#include <uWebSockets/HttpContext.h>
#include <string>

std::string NewAuthToken();
bool ValidateAuthToken(uWS::HttpRequest* http_request, const std::string& required_token);

#endif // CARTA_BACKEND__UTIL_TOKEN_H_
