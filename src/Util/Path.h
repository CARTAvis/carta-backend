/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__UTIL_PATH_H_
#define CARTA_BACKEND__UTIL_PATH_H_

#include "FileSystem.h"

bool CheckFolderPaths(std::string& top_level_string, std::string& starting_string);
int GetNumItems(const std::string& path);
fs::path SearchPath(std::string filename);

#endif // CARTA_BACKEND__UTIL_PATH_H_
