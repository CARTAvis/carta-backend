/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_SRC_SESSIONMANAGER_PROGRAMSETTINGS_H_
#define CARTA_BACKEND_SRC_SESSIONMANAGER_PROGRAMSETTINGS_H_

#include "Logger/Logger.h"

#include <iostream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>
#include "Constants.h"

namespace carta {
struct ProgramSettings {
    bool version = false;
    bool help = false;
    int port = -1;
    int grpc_port = -1;
    int omp_thread_count = OMP_THREAD_COUNT;
    std::string top_level_folder = "/";
    std::string starting_folder = ".";
    std::string host = "0.0.0.0";
    std::vector<std::string> files;
    std::string frontend_folder;
    bool no_http = false;
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

    bool no_user_config = false;
    bool no_system_config = false;

    nlohmann::json command_line_settings;
    bool system_settings_json_exists = false;
    bool user_settings_json_exists = false;

    // clang-format off
    std::unordered_map<std::string, int*> int_keys_map{
        {"verbosity", &verbosity},
        {"port", &port},
        {"grpc_port", &grpc_port},
        {"omp_threads", &omp_thread_count},
        {"exit_timeout", &wait_time},
        {"initial_timeout", &init_wait_time},
        {"idle_timeout", &idle_session_wait_time}
    };

    std::unordered_map<std::string, bool*> bool_keys_map{
        {"no_log", &no_log},
        {"log_performance", &log_performance},
        {"log_protocol_messages", &log_protocol_messages},
        {"no_http", &no_http},
        {"no_browser", &no_browser},
        {"read_only_mode", &read_only_mode}
    };

    std::unordered_map<std::string, std::string*> strings_keys_map{
        {"host", &host},
        {"top_level_folder", &top_level_folder},
        {"frontend_folder", &frontend_folder}
    };
    // clang-format on

    ProgramSettings() = default;
    ProgramSettings(int argc, char** argv);
    void ApplyCommandLineSettings(int argc, char** argv);
    nlohmann::json JSONSettingsFromFile(const std::string& fsp);
    void SetSettingsFromJSON(const nlohmann::json& j);

    auto GetTuple() const {
        return std::tie(help, version, port, grpc_port, omp_thread_count, top_level_folder, starting_folder, host, files, frontend_folder,
            no_http, no_browser, no_log, log_performance, log_protocol_messages, debug_no_auth, verbosity, wait_time, init_wait_time,
            idle_session_wait_time);
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
#endif // CARTA_BACKEND_SRC_SESSIONMANAGER_PROGRAMSETTINGS_H_
