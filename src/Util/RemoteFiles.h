/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_UTIL_REMOTEFILES_H_
#define CARTA_SRC_UTIL_REMOTEFILES_H_

#include <carta-protobuf/remote_file_request.pb.h>
#include <string>

// shouldn't these be in an ENV file?
const auto HIPS_BASE_URL = "https://alasky.cds.unistra.fr/hips-image-services/hips2fits";
const auto HIPS_MAX_PIXELS = 50e6;

/**
 * @brief Generates a URL for a remote file request based on provided parameters.
 *
 * This function validates the `CARTA::RemoteFileRequest` parameters and constructs a URL
 * to fetch an image from the HiPS service. If any required parameters are missing or invalid,
 * it returns `false` and provides an appropriate error message.
 *
 * @param request The `CARTA::RemoteFileRequest` containing the request parameters.
 * @param url The generated URL to be returned if the request is valid.
 * @param error_message A message indicating the reason for failure if the request is invalid.
 * @return `true` if the URL was successfully generated, `false` otherwise.
 *
 * @note The function ensures that all required parameters are present and valid before constructing the URL.
 *       If `wcs` is not provided, additional fields such as `coordsys`, `projection`, `fov`, and `object` or `(ra, dec)` must be specified.
 */
bool GenerateUrlFromRequest(const CARTA::RemoteFileRequest& request, std::string& url, std::string& error_message);

#endif // CARTA_SRC_UTIL_REMOTEFILES_H_
