#ifndef CARTA_BACKEND__UTIL_TOKEN_H_
#define CARTA_BACKEND__UTIL_TOKEN_H_

bool ValidateAuthToken(uWS::HttpRequest* http_request, const std::string& required_token);
bool ConstantTimeStringCompare(const std::string& a, const std::string& b);

#endif // CARTA_BACKEND__UTIL_TOKEN_H_
