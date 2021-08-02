/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ProgramSettingsHelpers.h"
#include <nlohmann/json.hpp>
#include "Logger/Logger.h"
#ifdef _BOOST_FILESYSTEM_
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif
#include <fstream>
#include <string>

using json = nlohmann::json;

namespace carta {
namespace ProgramSettingsHelpers {

void SaveLastDirectory(const std::string& address, const std::string& folder) {
    spdlog::debug("coconut... Saving last used directory {}, [{}]", folder, address);
    const fs::path settings_file_path = fs::path(getenv("HOME")) / CARTA_USER_FOLDER_PREFIX / "backend-known-connections.json";
    if (fs::exists(settings_file_path)) {
        // read previous file, and parse to json
        json settings;
        std::ifstream ifs(settings_file_path, std::ifstream::in);
        try {
            settings = json::parse(ifs, nullptr, true, true);
        } catch (json::exception& err) {
            spdlog::debug("Error parsing config file {}", settings_file_path.string());
            spdlog::debug(err.what());
        }
        ifs.close();
        // update and write
        if (settings.contains(address)) {
            // update the last dir
            spdlog::debug("Updating config file {}, with {}", settings_file_path.string(), folder);
            settings[address] = folder;
        } else {
            spdlog::debug("Adding a new entry to the config file {}, with {}", settings_file_path.string(), folder);
            settings[address] = folder;
        }
        std::ofstream file(settings_file_path, std::ofstream::out);
        file << settings;
        file.close();
    } else {
        // create the json object and write
        json settings;
        settings[address] = folder;
        std::ofstream file(settings_file_path);
        file << settings;
        file.close();
    }
}

void GetLastDirectory(const std::string& address, std::string& folder) {
    const fs::path settings_file_path = fs::path(getenv("HOME")) / CARTA_USER_FOLDER_PREFIX / "backend-known-connections.json";
    if (fs::exists(settings_file_path)) {
        std::ifstream ifs(settings_file_path, std::ifstream::in);
        json settings;
        try {
            settings = json::parse(ifs, nullptr, true, true);
        } catch (json::exception& err) {
            spdlog::debug("Error parsing config file {}", settings_file_path.string());
            spdlog::debug(err.what());
        }
        ifs.close();
        if (settings.contains(address)) {
            folder = settings[address];
        } else {
            folder = "";
        }
    } else {
        folder = "";
    }
}

} // namespace ProgramSettingsHelpers
} // namespace carta
