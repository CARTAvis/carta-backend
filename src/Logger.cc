/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Logger.h"

#include <filesystem>

namespace fs = std::filesystem;

void CreateLogger(const bool& no_log_file, const bool& debug_log) {
    std::string log_fullname = fs::path(getenv("HOME")).string() + "/CARTA/log/carta.log";

    // Set a console
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_pattern(LOG_PATTERN);

    std::vector<spdlog::sink_ptr> stdout_sinks;
    stdout_sinks.push_back(console_sink);
    if (!no_log_file) {
        // Set a log file with its full name, maximum size and the number of rotated files
        auto log_file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(log_fullname, LOG_FILE_SIZE, ROTATED_LOG_FILES);
        log_file_sink->set_pattern(LOG_PATTERN);
        stdout_sinks.push_back(log_file_sink);
    }

    // Create a logger
    auto stdout_logger = std::make_shared<spdlog::logger>(LOG_TAG, std::begin(stdout_sinks), std::end(stdout_sinks));
    if (debug_log) { // show log messages from the "debug" level, otherwise only show default messages from the "info" level
        stdout_logger->set_level(spdlog::level::debug);
    }
    spdlog::register_logger(stdout_logger);
}