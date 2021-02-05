/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Logger.h"

#ifdef _BOOST_FILESYSTEM_
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

namespace fs = std::filesystem;

void InitLoggers(bool no_log_file, bool debug_log, bool perf_log) {
    std::string log_fullname;
    if (!no_log_file) {
        log_fullname = fs::path(getenv("HOME")).string() + "/.carta/log/carta.log";
        spdlog::info("Writing to the log file: {}", log_fullname);
    }

    // Set the stdout console
    auto stdout_console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    stdout_console_sink->set_pattern(STDOUT_PATTERN);

    // Set stdout sinks
    std::vector<spdlog::sink_ptr> stdout_sinks;
    stdout_sinks.push_back(stdout_console_sink);

    if (!no_log_file) {
        // Set a log file with its full name, maximum size and the number of rotated files
        auto stdout_log_file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(log_fullname, LOG_FILE_SIZE, ROTATED_LOG_FILES);
        stdout_log_file_sink->set_pattern(STDOUT_PATTERN);
        stdout_sinks.push_back(stdout_log_file_sink);
    }

    // Create the stdout logger
    auto stdout_logger = std::make_shared<spdlog::logger>(STDOUT_TAG, std::begin(stdout_sinks), std::end(stdout_sinks));

    // show log messages from the "debug" level, otherwise only show default messages from the "info" level
    if (debug_log) {
        stdout_logger->set_level(spdlog::level::debug);
    }

    // Register the stdout logger
    if (!spdlog::get(STDOUT_TAG)) {
        spdlog::register_logger(stdout_logger);
    } else {
        spdlog::critical("Duplicate registration of the logger: {}!", STDOUT_TAG);
    }

    // Show the carta_backend executor version via the stdout logger
    std::string current_path = fs::current_path();
    stdout_logger->info("{}/carta_backend: Version {}", current_path, VERSION_ID);

    if (perf_log) {
        // Set the perf console
        auto perf_console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        perf_console_sink->set_pattern(CUSTOMIZE_PATTERN);

        // Set perf sinks
        std::vector<spdlog::sink_ptr> perf_sinks;
        perf_sinks.push_back(perf_console_sink);

        if (!no_log_file) {
            // Set a log file with its full name, maximum size and the number of rotated files
            auto perf_log_file_sink =
                std::make_shared<spdlog::sinks::rotating_file_sink_mt>(log_fullname, LOG_FILE_SIZE, ROTATED_LOG_FILES);
            perf_log_file_sink->set_pattern(CUSTOMIZE_PATTERN);
            perf_sinks.push_back(perf_log_file_sink);
        }

        // Create the perf logger
        auto perf_logger = std::make_shared<spdlog::logger>(PERF_TAG, std::begin(perf_sinks), std::end(perf_sinks));
        perf_logger->set_level(spdlog::level::debug);

        // Register the perf logger
        if (!spdlog::get(PERF_TAG)) {
            spdlog::register_logger(perf_logger);
        } else {
            spdlog::critical("Duplicate registration of the logger: {}!", PERF_TAG);
        }
    }
}