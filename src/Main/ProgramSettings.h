/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_SRC_MAIN_PROGRAMSETTINGS_H_
#define CARTA_BACKEND_SRC_MAIN_PROGRAMSETTINGS_H_

#include <iostream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "Logger/Logger.h"
#include "Util/FileSystem.h"

#define OMP_THREAD_COUNT -1
#define DEFAULT_SOCKET_PORT 3002
#define RESERVED_MEMORY 0 // GB

#ifndef CARTA_DEFAULT_FRONTEND_FOLDER
#define CARTA_DEFAULT_FRONTEND_FOLDER "../share/carta/frontend"
#endif

#ifndef CARTA_USER_FOLDER_PREFIX
#define CARTA_USER_FOLDER_PREFIX ".carta"
#endif

namespace carta {
struct ProgramSettings {
    bool version = false;
    bool help = false;
    std::vector<int> port;
    int omp_thread_count = OMP_THREAD_COUNT;
    int event_thread_count = 2;
    std::string top_level_folder = "/";
    std::string starting_folder = ".";
    std::string host = "0.0.0.0";
    std::vector<std::string> files;
    std::vector<fs::path> file_paths;
    std::string frontend_folder;
    bool no_http = false; // Deprecated
    bool no_frontend = false;
    bool no_database = false;
    bool no_runtime_config = false;
    bool debug_no_auth = false;
    bool no_browser = false;
    bool no_log = false;
    bool log_performance = false;
    bool log_protocol_messages = false;
    int verbosity = 4;
    int wait_time = -1;
    int init_wait_time = -1;
    int idle_session_wait_time = -1;
    bool read_only_mode = false;
    bool enable_scripting = false;
    float reserved_memory = RESERVED_MEMORY;

    std::string browser;

    bool no_user_config = false;
    bool no_system_config = false;

    nlohmann::json command_line_settings;
    bool system_settings_json_exists = false;
    bool user_settings_json_exists = false;

    fs::path user_directory;

    // clang-format off
    std::unordered_map<std::string, int*> int_keys_map{
        {"verbosity", &verbosity},
        {"omp_threads", &omp_thread_count},
        {"event_thread_count", &event_thread_count},
        {"exit_timeout", &wait_time},
        {"initial_timeout", &init_wait_time},
        {"idle_timeout", &idle_session_wait_time}
    };

    std::unordered_map<std::string, float*> float_keys_map{
        {"reserved_memory", &reserved_memory}
    };

    std::unordered_map<std::string, bool*> bool_keys_map{
        {"no_log", &no_log},
        {"log_performance", &log_performance},
        {"log_protocol_messages", &log_protocol_messages},
        {"no_http", &no_http}, // Deprecated
        {"no_browser", &no_browser},
        {"read_only_mode", &read_only_mode},
        {"enable_scripting", &enable_scripting},
        {"no_frontend", &no_frontend},
        {"no_database", &no_database},
        {"no_runtime_config", &no_runtime_config}
    };

    std::unordered_map<std::string, std::string*> strings_keys_map{
        {"host", &host},
        {"top_level_folder", &top_level_folder},
        {"starting_folder", &starting_folder},
        {"frontend_folder", &frontend_folder},
        {"browser", &browser}
    };

    std::unordered_map<std::string, std::vector<int>*> vector_int_keys_map {
        {"port", &port}
    };
    
    std::unordered_map<std::string, std::string> deprecated_options {
        {"base", "Use positional parameters instead to set the starting directory or open files on startup."},
        {"root", "Use top_level_folder instead."},
        {"threads", "This feature is no longer supported."},
        {"no_http", "Use no_frontend and no_database instead."}
    };
    // clang-format on

    ProgramSettings() = default;
    ProgramSettings(int argc, char** argv);
    void ApplyCommandLineSettings(int argc, char** argv);
    void ApplyJSONSettings();
    void AddDeprecationWarning(const std::string& option, std::string where);
    nlohmann::json JSONSettingsFromFile(const std::string& fsp);
    void SetSettingsFromJSON(const nlohmann::json& j);
    void PushFilePaths();

    // TODO: this is outdated. It's used by the equality operator, which is used by a test.
    auto GetTuple() const {
        return std::tie(help, version, port, omp_thread_count, top_level_folder, starting_folder, host, files, frontend_folder, no_http,
            no_browser, no_log, log_performance, log_protocol_messages, debug_no_auth, verbosity, wait_time, init_wait_time,
            idle_session_wait_time, reserved_memory);
    }
    bool operator!=(const ProgramSettings& rhs) const;
    bool operator==(const ProgramSettings& rhs) const;

    std::vector<std::string> warning_msgs;
    std::vector<std::string> debug_msgs;
    void FlushMessages() {
        std::for_each(warning_msgs.begin(), warning_msgs.end(), [](const std::string& msg) { spdlog::warn(msg); });
        std::for_each(debug_msgs.begin(), debug_msgs.end(), [](const std::string& msg) { spdlog::debug(msg); });
        warning_msgs.clear();
        debug_msgs.clear();
    }
};
} // namespace carta
#endif // CARTA_BACKEND_SRC_MAIN_PROGRAMSETTINGS_H_
