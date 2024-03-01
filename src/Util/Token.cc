/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2024 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Token.h"
#include "String.h"

#include <regex>

#include <uuid/uuid.h>

std::string NewAuthToken() {
    uuid_t token;
    char token_string[37];
    uuid_generate_random(token);
    uuid_unparse(token, token_string);
    return std::string(token_string);
}

bool ValidateAuthToken(uWS::HttpRequest* http_request, const std::string& required_token) {
    // Always allow if the required token is empty
    if (required_token.empty()) {
        return true;
    }
    // First try the cookie auth token
    std::string cookie_header = std::string(http_request->getHeader("cookie"));
    if (!cookie_header.empty()) {
        std::regex header_regex("carta-auth-token=(.+?)(?:;|$)");
        std::smatch sm;
        if (std::regex_search(cookie_header, sm, header_regex) && sm.size() == 2 && sm[1] == required_token) {
            return true;
        }
    }

    // Try the standard authorization bearer token approach
    std::string auth_header = std::string(http_request->getHeader("authorization"));
    std::regex auth_regex(R"(^Bearer\s+(\S+)$)");
    std::smatch sm;
    if (std::regex_search(auth_header, sm, auth_regex) && sm.size() == 2 && ConstantTimeStringCompare(sm[1], required_token)) {
        return true;
    }

    // Try the URL query
    auto query_token = http_request->getQuery("token");
    if (!query_token.empty() && ConstantTimeStringCompare(std::string(query_token), required_token)) {
        return true;
    }
    // Finally, fall back to the non-standard auth token header
    return ConstantTimeStringCompare(std::string(http_request->getHeader("carta-auth-token")), required_token);
}
