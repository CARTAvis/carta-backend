/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2024 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "String.h"

#include <iomanip>
#include <regex>
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

std::string SafeStringUnescape(const std::string& input) {
    // Adapted from https://gist.github.com/arthurafarias/56fec2cd49a32f374c02d1df2b6c350f
    // Replaces hex tokens in the form %XX to their ASCII equivalent

    auto decoded = input;
    int dynamicLength = decoded.size() - 2;

    // Skip processing for strings shorter than one escaped character %DD
    if (decoded.size() < 3) {
        return decoded;
    }

    for (int i = 0; i < dynamicLength; i++) {
        std::string current_substring = decoded.substr(i, 3);

        std::smatch sm;
        if (std::regex_match(current_substring, sm, std::regex("%[0-9A-F]{2}"))) {
            current_substring = current_substring.replace(0, 1, "0x");
            std::string replacement_character = {(char)std::stoi(current_substring, nullptr, 16)};
            decoded = decoded.replace(decoded.begin() + i, decoded.begin() + i + 3, replacement_character);
        }
        dynamicLength = decoded.size() - 2;
    }

    return decoded;
}

bool StringToInt(const std::string& input, int& i) {
    try {
        i = std::stoi(input);
        return true;
    } catch (std::invalid_argument) {
        return false;
    } catch (std::out_of_range) {
        return false;
    }
}
