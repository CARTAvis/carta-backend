/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_SRC_SESSIONMANAGER_PROGRAMSETTINGS_H_
#define CARTA_BACKEND_SRC_SESSIONMANAGER_PROGRAMSETTINGS_H_

#include <iostream>
#include <set>
#include <string>
#include <tuple>
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

    bool no_user_config = false;
    bool no_system_config = false;

    nlohmann::json command_line_settings;
    bool system_settings_json_exists = false;
    bool user_settings_json_exists = false;

    std::vector<std::string> int_keys = {
        "verbosity", "port", "grpc_port", "omp_threads", "exit_timeout", "initial_timeout", "idle_timeout"};
    std::vector<std::string> bool_keys = {"no_log", "log_performance", "log_protocol_messages", "no_http", "no_browser"};
    std::vector<std::string> string_keys = {"host", "top_level_folder", "frontend_folder"};
    std::set<std::string> invalid_keys; // hold keys that are not valid

    ProgramSettings() = default;
    ProgramSettings(int argc, char** argv);
    void ApplyCommandLineSettings(int argc, char** argv);
    nlohmann::json JSONConfigSettings(const std::string& fsp);
    void SetSettingsFromJSON(const nlohmann::json& j);
    void ValidateJSON(const nlohmann::json& j);

    auto GetTuple() const {
        return std::tie(help, version, port, grpc_port, omp_thread_count, top_level_folder, starting_folder, host, files, frontend_folder,
            no_http, no_browser, no_log, log_performance, log_protocol_messages, debug_no_auth, verbosity, wait_time, init_wait_time,
            idle_session_wait_time);
    }
    bool operator!=(const ProgramSettings& rhs) const;
    bool operator==(const ProgramSettings& rhs) const;
};
} // namespace carta
#endif // CARTA_BACKEND_SRC_SESSIONMANAGER_PROGRAMSETTINGS_H_
