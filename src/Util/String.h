/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_UTIL_STRING_H_
#define CARTA_SRC_UTIL_STRING_H_

#include <string>
#include <vector>

// Escape URL strings
std::string SafeStringEscape(const std::string& input);
// Unescape URL strings
std::string SafeStringUnescape(const std::string& input);

// split input string into a vector of strings by delimiter
void SplitString(std::string& input, char delim, std::vector<std::string>& parts);

// determines whether a string ends with another given string
bool HasSuffix(const std::string& haystack, const std::string& needle, bool case_sensitive = false);

// determine whether strings are equal in constant time, rather than based on early-exit
bool ConstantTimeStringCompare(const std::string& a, const std::string& b);

// Convert string to integer, return whether success
bool StringToInt(const std::string& input, int& i);

#endif // CARTA_SRC_UTIL_STRING_H_
