/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021, 2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__UTIL_APP_H_
#define CARTA_BACKEND__UTIL_APP_H_

#include <string>

// version
#define VERSION_ID "3.0.0-beta.3"

bool FindExecutablePath(std::string& path);
std::string GetReleaseInformation();

#endif // CARTA_BACKEND__UTIL_APP_H_
