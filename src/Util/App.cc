/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
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

std::string ExecuteCommand(const char* command) {
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
