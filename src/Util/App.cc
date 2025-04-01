/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "App.h"

#include <unistd.h>
#include <climits>
#include <fstream>
#include <sstream>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <stdio.h>
#endif

#include "FileSystem.h"
#include "Logger/Logger.h"

#define MAX_PLATFORM_INFO_LENGTH 1024
#define MAX_PLATFORM_LINE_LENGTH 256

/**
 * @details This function determines the full path of the running executable and stores it in the `path` parameter.
 * It supports both macOS (`_NSGetExecutablePath`) and Linux (`/proc/self/exe`).
 *
 * @note On macOS, `_NSGetExecutablePath` is used, and the buffer size is checked dynamically.
 * @note On Linux, the function reads from `/proc/self/exe` using `readlink()`.
 *
 * @warning On macOS, if the buffer is too small, `_NSGetExecutablePath` may fail,
 *          this implementation does not handle resizing the buffer.
 */
bool FindExecutablePath(std::string& path) {
    char path_buffer[PATH_MAX + 1];
#ifdef __APPLE__
    uint32_t len = sizeof(path_buffer);

    if (_NSGetExecutablePath(path_buffer, &len) != 0) {
        return false;
    }
#else
    const int len = int(readlink("/proc/self/exe", path_buffer, PATH_MAX));

    if (len == -1) {
        return false;
    }

    path_buffer[len] = 0;
#endif
    path = path_buffer;
    return true;
}

/**
 * @details This function fetches OS release details using platform-specific methods:
 * - On **macOS**, it runs the `sw_vers` command and reads the output.
 * - On **Linux/Unix**, it attempts to read `/etc/os-release`, which is available on most distributions using systemd.
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
std::string GetReleaseInformation() {
#ifdef __APPLE__
    // MacOS solution adapted from https://stackoverflow.com/a/44684199/1727322
    char info_buffer[MAX_PLATFORM_INFO_LENGTH];
    unsigned buffer_length = 0;
    char line[MAX_PLATFORM_LINE_LENGTH];
    auto file_handle = popen("sw_vers", "r");
    while (fgets(line, sizeof(line), file_handle) != nullptr) {
        int l = snprintf(info_buffer + buffer_length, sizeof(info_buffer) - buffer_length, "%s", line);
        buffer_length += l;
        if (buffer_length > MAX_PLATFORM_INFO_LENGTH) {
            spdlog::warn("Problem reading platform information");
            return std::string("Platform information not available");
        }
    }
    pclose(file_handle);
    return info_buffer;
#else
    // Unix solution just attempts to read from /etc/os-release. This works with Ubuntu, RedHat, CentOS, Arch, Debian and Fedora,
    // and should work on any system that has systemd installed
    fs::path path = "/etc/os-release";
    std::error_code error_code;

    if (fs::exists(path, error_code) && fs::is_regular_file(path, error_code)) {
        try {
            // read the entire release file to string
            std::ifstream input_file(path);
            if (input_file.good()) {
                std::stringstream buffer;
                buffer << input_file.rdbuf();
                return buffer.str();
            }
        } catch (std::ifstream::failure e) {
            spdlog::warn("Problem reading platform information");
        }
    }
#endif
    return std::string("Platform information not available");
}

/**
 * @details This function opens a pipe using `popen()` to execute the given command,
 * reads its output, and returns it as a `std::string`.
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
std::string OutputOfCommand(const char* command) {
    std::ostringstream output;

    // Open a pipe to execute the command and read its output
    FILE* pipe = popen(command, "r");
    if (!pipe) {
        std::cerr << "Error executing command." << std::endl;
        return "";
    }

    // Read the command output and store it in the output stream
    char buffer[128];
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        output << buffer;
    }

    // Close the pipe
    pclose(pipe);

    std::string result = output.str().erase(output.str().find_last_not_of("\n") + 1);

    return result;
}
