/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_UTIL_REMOTEFILES_H_
#define CARTA_SRC_UTIL_REMOTEFILES_H_

#include <carta-protobuf/remote_file_request.pb.h>
#include <string>

const auto HIPS_BASE_URL = "https://alasky.cds.unistra.fr/hips-image-services/hips2fits";
const auto HIPS_MAX_PIXELS = 50e6;
bool GenerateUrlFromRequest(const CARTA::RemoteFileRequest& request, std::string& url, std::string& error_message);

#endif // CARTA_SRC_UTIL_REMOTEFILES_H_
