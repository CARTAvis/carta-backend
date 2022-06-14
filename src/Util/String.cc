/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018 - 2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "String.h"

#include <iomanip>
#include <sstream>

void SplitString(std::string& input, char delim, std::vector<std::string>& parts) {
    // util to split input string into parts by delimiter
    parts.clear();
    std::stringstream ss(input);
    std::string item;
    while (getline(ss, item, delim)) {
        if (!item.empty()) {
            if (item.back() == '\r') {
                item.pop_back();
            }
            parts.push_back(item);
        }
    }
}

bool HasSuffix(const std::string& haystack, const std::string& needle, bool case_sensitive) {
    if (needle.size() > haystack.size()) {
        return false;
    }

    if (case_sensitive) {
        return std::equal(needle.rbegin(), needle.rend(), haystack.rbegin());
    } else {
        return std::equal(needle.rbegin(), needle.rend(), haystack.rbegin(), [](char a, char b) { return tolower(a) == tolower(b); });
    }
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

std::string SafeStringEscape(const std::string& input) {
    // Adapted from https://stackoverflow.com/a/17708801
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (const auto c : input) {
        // Keep alphanumeric and other accepted characters intact
        if (isalnum(c, std::locale::classic()) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
            continue;
        }

        // Any other characters are percent-encoded
        escaped << std::uppercase;
        escaped << '%' << std::setw(2) << int((unsigned char)c);
        escaped << std::nouppercase;
    }

    return escaped.str();
}
