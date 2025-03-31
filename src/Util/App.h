/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_UTIL_APP_H_
#define CARTA_SRC_UTIL_APP_H_

#include <string>

// version
#define VERSION_ID "5.0.0-dev"

/**
 * @brief Retrieves the absolute path of the currently running executable.
 *
 * This function determines the full path of the running executable and stores it in the `path` parameter.
 * It supports both macOS (`_NSGetExecutablePath`) and Linux (`/proc/self/exe`).
 *
 * @param[out] path A reference to a string that will store the resolved executable path if successful.
 *
 * @return `true` if the executable path was successfully retrieved, `false` otherwise.
 *
 * @note On macOS, `_NSGetExecutablePath` is used, and the buffer size is checked dynamically.
 * @note On Linux, the function reads from `/proc/self/exe` using `readlink()`.
 *
 * @warning On macOS, if the buffer is too small, `_NSGetExecutablePath` may fail,
 *          this implementation does not handle resizing the buffer.
 */
bool FindExecutablePath(std::string& path);

/**
 * @brief Retrieves the operating system release information.
 *
 * This function fetches OS release details using platform-specific methods:
 * - On **macOS**, it runs the `sw_vers` command and reads the output.
 * - On **Linux/Unix**, it attempts to read `/etc/os-release`, which is available on most distributions using systemd.
 *
 * @return A `std::string` containing the OS release information.
 * If the information is unavailable or an error occurs, the function returns `"Platform information not available"`.
 *
 * @note
 * - On macOS, `popen("sw_vers", "r")` is used to execute `sw_vers`, which outputs OS version details.
 * - On Linux, the function reads the `/etc/os-release` file into a string.
 * - If an error occurs while reading the file, a warning is logged via `spdlog::warn`.
 *
 * @warning
 * - The function does **not** support Windows.
 * - On macOS, buffer overflow protection is in place, but the buffer size is fixed.
 */
std::string GetReleaseInformation();

/**
 * @brief Executes a shell command and returns its output.
 *
 * This function opens a pipe using `popen()` to execute the given command,
 * reads its output, and returns it as a `std::string`.
 *
 * @param command The shell command to execute.
 *
 * @return A `std::string` containing the command's output.
 * If execution fails, an empty string is returned.
 *
 * @note
 * - The function trims trailing newline characters from the output.
 * - The caller must ensure the command is safe to execute (avoid shell injection).
 * - Works only on Unix-like systems; **not compatible with Windows** (consider `_popen()` for Windows support).
 *
 * @warning
 * - If `popen()` fails, an error message is printed, and an empty string is returned.
 * - The function does not handle command execution failures beyond detecting if `popen()` fails.
 */
std::string OutputOfCommand(const char* command);

#endif // CARTA_SRC_UTIL_APP_H_
