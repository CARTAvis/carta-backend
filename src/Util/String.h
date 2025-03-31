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
 * This function converts special characters in the input string into
 * percent-encoded format, making it safe for use in URLs or other
 * contexts where special characters need to be encoded. Alphanumeric
 * characters and `-`, `_`, `.`, and `~` are left unchanged.
 *
 * @param[in] input The string to be escaped.
 *
 * @return A percent-encoded version of the input string.
 */
std::string SafeStringEscape(const std::string& input);

/**
 * @brief Decodes a percent-encoded (URL-encoded) string.
 *
 * This function converts percent-encoded sequences (e.g., `%20` for space)
 * back into their ASCII character equivalents. It scans the input string
 * and replaces any valid `%XX` hexadecimal escape sequences with their
 * corresponding characters.
 *
 * @param[in] input The percent-encoded string to be decoded.
 *
 * @return A decoded version of the input string with percent-encoded
 *         sequences replaced by their ASCII equivalents.
 *
 * @note The function assumes that the input is a properly formatted
 *       percent-encoded string. If an invalid escape sequence is encountered,
 *       it is left unchanged.
 */
std::string SafeStringUnescape(const std::string& input);

/**
 * @brief Splits a string into parts based on a specified delimiter.
 *
 * This function tokenizes an input string by the given delimiter and stores
 * the resulting substrings in a vector. It also removes trailing carriage
 * return (`\r`) characters from each token.
 *
 * @param[in] input The string to be split.
 * @param[in] delim The character delimiter used to separate the input string.
 * @param[out] parts A vector to store the resulting substrings. It is cleared before adding new values.
 *
 * @note Empty tokens are ignored, so consecutive delimiters will not produce empty strings in `parts`.
 *
 * @warning The function modifies the `parts` vector by clearing its contents before adding new elements.
 */
void SplitString(std::string& input, char delim, std::vector<std::string>& parts);

/**
 * @brief Checks if a string ends with a specified suffix.
 *
 * This function determines whether the `haystack` string ends with the `needle` string.
 * It supports both case-sensitive and case-insensitive comparisons.
 *
 * @param[in] haystack The string to be checked.
 * @param[in] needle The suffix to look for.
 * @param[in] case_sensitive If `true`, performs a case-sensitive comparison; if `false`, ignores case differences.
 *
 * @return `true` if `haystack` ends with `needle`, otherwise `false`.
 *
 * @note If `needle` is longer than `haystack`, the function immediately returns `false`.
 */
bool HasSuffix(const std::string& haystack, const std::string& needle, bool case_sensitive = false);

/**
 * @brief Performs a constant-time comparison of two strings.
 *
 * This function compares two strings in a way that avoids timing attacks
 * by ensuring that the execution time does not depend on the input values.
 * It XORs each corresponding character and accumulates the differences,
 * preventing early termination.
 *
 * @param[in] a The first string to compare.
 * @param[in] b The second string to compare.
 *
 * @return `true` if both strings are equal, otherwise `false`.
 *
 * @note If the strings have different lengths, the function immediately returns `false`.
 */
bool ConstantTimeStringCompare(const std::string& a, const std::string& b);

/**
 * @brief Converts a string to an integer.
 *
 * Attempts to parse an integer from the given string. If successful,
 * the parsed integer is stored in the output parameter, and the function
 * returns `true`. If the conversion fails due to an invalid format or
 * an out-of-range value, the function returns `false`.
 *
 * @param[in] input The string to be converted to an integer.
 * @param[out] i Reference to an integer where the result will be stored if conversion succeeds.
 *
 * @return `true` if the conversion is successful, `false` otherwise.
 *
 * @note This function does not modify `i` if the conversion fails.
 */
bool StringToInt(const std::string& input, int& i);

#endif // CARTA_SRC_UTIL_STRING_H_
