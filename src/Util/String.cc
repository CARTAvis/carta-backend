/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "String.h"

#include <iomanip>
#include <regex>
#include <sstream>

/**
 * @details This function tokenizes an input string by the given delimiter and stores
 * the resulting substrings in a vector. It also removes trailing carriage
 * return (`\r`) characters from each token.
 *
 * @note Empty tokens are ignored, so consecutive delimiters will not produce empty strings in `parts`.
 *
 * @warning The function modifies the `parts` vector by clearing its contents before adding new elements.
 */
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

/**
 * @details This function determines whether the `haystack` string ends with the `needle` string.
 * It supports both case-sensitive and case-insensitive comparisons.
 *
 * @return `true` if `haystack` ends with `needle`, otherwise `false`.
 *
 * @note If `needle` is longer than `haystack`, the function immediately returns `false`.
 */
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

/**
 * @details This function compares two strings in a way that avoids timing attacks
 * by ensuring that the execution time does not depend on the input values.
 * It XORs each corresponding character and accumulates the differences,
 * preventing early termination.
 *
 * @note If the strings have different lengths, the function immediately returns `false`.
 */
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

/**
 * @details This function converts special characters in the input string into
 * percent-encoded format, making it safe for use in URLs or other
 * contexts where special characters need to be encoded. Alphanumeric
 * characters and `-`, `_`, `.`, and `~` are left unchanged.
 */
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

/**
 * @details This function converts percent-encoded sequences (e.g., `%20` for space)
 * back into their ASCII character equivalents. It scans the input string
 * and replaces any valid `%XX` hexadecimal escape sequences with their
 * corresponding characters.
 *
 * @note The function assumes that the input is a properly formatted
 *       percent-encoded string. If an invalid escape sequence is encountered,
 *       it is left unchanged.
 */
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

/**
 * @details Attempts to parse an integer from the given string. If successful,
 * the parsed integer is stored in the output parameter, and the function
 * returns `true`. If the conversion fails due to an invalid format or
 * an out-of-range value, the function returns `false`.
 *
 * @note This function does not modify `i` if the conversion fails.
 */
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
