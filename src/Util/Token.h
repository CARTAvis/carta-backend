/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

/** @file
 *  Utilities for working with authentication tokens.
 */

#ifndef CARTA_SRC_UTIL_TOKEN_H_
#define CARTA_SRC_UTIL_TOKEN_H_

#include <uWebSockets/HttpContext.h>
#include <string>

/** @brief Create a new authentication token.
 *  @return A new random token.
 */
std::string NewAuthToken();

/** @brief Validate an HTTP request.
 *  @param http_request The HTTP request to validate.
 *  @param required_token The token to use for authentication.
 *  @return Whether the request contains the required authentication token.
 */
bool ValidateAuthToken(uWS::HttpRequest* http_request, const std::string& required_token);

#endif // CARTA_SRC_UTIL_TOKEN_H_
