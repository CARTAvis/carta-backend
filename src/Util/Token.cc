#include "Token.h"

bool ValidateAuthToken(uWS::HttpRequest* http_request, const string& required_token) {
    // Always allow if the required token is empty
    if (required_token.empty()) {
        return true;
    }
    // First try the cookie auth token
    string cookie_header = string(http_request->getHeader("cookie"));
    if (!cookie_header.empty()) {
        regex header_regex("carta-auth-token=(.+?)(?:;|$)");
        smatch sm;
        if (regex_search(cookie_header, sm, header_regex) && sm.size() == 2 && sm[1] == required_token) {
            return true;
        }
    }

    // Try the standard authorization bearer token approach
    string auth_header = string(http_request->getHeader("authorization"));
    regex auth_regex(R"(^Bearer\s+(\S+)$)");
    smatch sm;
    if (regex_search(auth_header, sm, auth_regex) && sm.size() == 2 && ConstantTimeStringCompare(sm[1], required_token)) {
        return true;
    }

    // Try the URL query
    auto query_token = http_request->getQuery("token");
    if (!query_token.empty() && ConstantTimeStringCompare(string(query_token), required_token)) {
        return true;
    }
    // Finally, fall back to the non-standard auth token header
    return ConstantTimeStringCompare(string(http_request->getHeader("carta-auth-token")), required_token);
}

bool ConstantTimeStringCompare(const std::string& a, const std::string& b) {
    // Early exit when lengths are unequal. This is not a problem in our case
    if (a.length() != b.length()) {
        return false;
    }

    volatile int d = 0;
    for (int i = 0; i < a.length(); i++) {
        d |= a[i] ^ b[i];
    }

    return d == 0;
}
