/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_UTIL_APP_H_
#define CARTA_SRC_UTIL_APP_H_

#include <string>

// version
#define VERSION_ID "5.1"

/**
 * @brief Retrieves the absolute path of the currently running executable.
 *
 * @param[out] path A reference to a string that will store the resolved executable path if successful.
 *
 * @return `true` if the executable path was successfully retrieved, `false` otherwise.
 */
bool FindExecutablePath(std::string& path);

/**
 * @brief Retrieves the operating system release information.
 *
 * @return A `std::string` containing the OS release information.
 * If the information is unavailable or an error occurs, the function returns `"Platform information not available"`.
 */
std::string GetReleaseInformation();

/**
 * @brief Executes a shell command and returns its output.
 *
 * @param command The shell command to execute.
 *
 * @return A `std::string` containing the command's output.
 * If execution fails, an empty string is returned.
 */
std::string OutputOfCommand(const char* command);

#endif // CARTA_SRC_UTIL_APP_H_
