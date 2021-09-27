/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__UTIL_STRING_H_
#define CARTA_BACKEND__UTIL_STRING_H_

#include <string>
#include <vector>

// split input string into a vector of strings by delimiter
void SplitString(std::string& input, char delim, std::vector<std::string>& parts);

#endif // CARTA_BACKEND__UTIL_STRING_H_
