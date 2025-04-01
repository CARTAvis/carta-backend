/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_UTIL_STRING_H_
#define CARTA_SRC_UTIL_STRING_H_

#include <string>
#include <vector>

/**
 * @brief Escapes a string using percent-encoding (URL encoding).
 *
 * @param[in] input The string to be escaped.
 *
 * @return A percent-encoded version of the input string.
 */
std::string SafeStringEscape(const std::string& input);

/**
 * @brief Decodes a percent-encoded (URL-encoded) string.
 *
 * @param[in] input The percent-encoded string to be decoded.
 *
 * @return A decoded version of the input string with percent-encoded
 *         sequences replaced by their ASCII equivalents.
 */
std::string SafeStringUnescape(const std::string& input);

/**
 * @brief Splits a string into parts based on a specified delimiter.
 *
 * @param[in] input The string to be split.
 * @param[in] delim The character delimiter used to separate the input string.
 * @param[out] parts A vector to store the resulting substrings. It is cleared before adding new values.
 */
void SplitString(std::string& input, char delim, std::vector<std::string>& parts);

/**
 * @brief Checks if a string ends with a specified suffix.
 *
 * @param[in] haystack The string to be checked.
 * @param[in] needle The suffix to look for.
 * @param[in] case_sensitive If `true`, performs a case-sensitive comparison; if `false`, ignores case differences.
 */
bool HasSuffix(const std::string& haystack, const std::string& needle, bool case_sensitive = false);

/**
 * @brief Performs a constant-time comparison of two strings.
 *
 * @param[in] a The first string to compare.
 * @param[in] b The second string to compare.
 *
 * @return `true` if both strings are equal, otherwise `false`.
 */
bool ConstantTimeStringCompare(const std::string& a, const std::string& b);

/**
 * @brief Converts a string to an integer.
 *
 * @param[in] input The string to be converted to an integer.
 * @param[out] i Reference to an integer where the result will be stored if conversion succeeds.
 *
 * @return `true` if the conversion is successful, `false` otherwise.
 */
bool StringToInt(const std::string& input, int& i);

#endif // CARTA_SRC_UTIL_STRING_H_
