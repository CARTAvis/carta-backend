/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Logger.h"

#include <filesystem>

namespace fs = std::filesystem;

void CreateLoggers(bool no_log_file, bool debug_log) {
    std::string log_fullname = fs::path(getenv("HOME")).string() + "/CARTA/log/carta.log";

    // Set a console
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_pattern(STDOUT_PATTERN);

    std::vector<spdlog::sink_ptr> stdout_sinks;
    stdout_sinks.push_back(console_sink);
    if (!no_log_file) {
        // Set a log file with its full name, maximum size and the number of rotated files
        auto log_file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(log_fullname, LOG_FILE_SIZE, ROTATED_LOG_FILES);
        log_file_sink->set_pattern(STDOUT_PATTERN);
        stdout_sinks.push_back(log_file_sink);
    }

    // Create a logger
    auto stdout_logger = std::make_shared<spdlog::logger>(STDOUT_TAG, std::begin(stdout_sinks), std::end(stdout_sinks));
    if (debug_log) { // show log messages from the "debug" level, otherwise only show default messages from the "info" level
        stdout_logger->set_level(spdlog::level::debug);
    }

    // Register a logger
    if (!spdlog::get(STDOUT_TAG)) {
        spdlog::register_logger(stdout_logger);
    } else {
        spdlog::critical("Duplicate registration of the logger: {}!", STDOUT_TAG);
    }

    // Show the carta_backend executor version
    std::string current_path = fs::current_path();
    stdout_logger->info("{}/carta_backend: Version {}", current_path, VERSION_ID);
}