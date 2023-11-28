/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__UTIL_APP_H_
#define CARTA_BACKEND__UTIL_APP_H_

#include <mutex>
#include <string>

// version
#define VERSION_ID "4.0.0-rc.0"

// Global variable
extern float FULL_IMAGE_CACHE_SIZE_AVAILABLE; // MB
extern std::mutex FULL_IMAGE_CACHE_SIZE_AVAILABLE_MUTEX;

bool FindExecutablePath(std::string& path);
std::string GetReleaseInformation();
std::string OutputOfCommand(const char* command);

#endif // CARTA_BACKEND__UTIL_APP_H_
