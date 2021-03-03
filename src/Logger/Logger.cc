/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Logger.h"
#include "Constants.h"

#ifdef _BOOST_FILESYSTEM_
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

static bool log_protocol_messages(false);

void InitLogger(bool no_log_file, int verbosity, bool log_performance, bool log_protocol_messages_) {
    log_protocol_messages = log_protocol_messages_;

    // Set the stdout console
    auto stdout_console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    stdout_console_sink->set_pattern(STDOUT_PATTERN);

    // Set stdout sinks
    std::vector<spdlog::sink_ptr> stdout_sinks;
    stdout_sinks.push_back(stdout_console_sink);

    // Set a log file with its full name, maximum size and the number of rotated files
    std::string log_fullname;
    if (!no_log_file) {
        log_fullname = (fs::path(getenv("HOME")) / CARTA_USER_FOLDER_PREFIX / "log/carta.log").string();
        auto stdout_log_file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(log_fullname, LOG_FILE_SIZE, ROTATED_LOG_FILES);
        stdout_log_file_sink->set_pattern(STDOUT_PATTERN);
        stdout_sinks.push_back(stdout_log_file_sink);
    }

    // Create the stdout logger
    auto default_logger = std::make_shared<spdlog::logger>(STDOUT_TAG, std::begin(stdout_sinks), std::end(stdout_sinks));

    // Set flush policy on severity
    default_logger->flush_on(spdlog::level::err);

    // Set the stdout logger level according to the verbosity number
    switch (verbosity) {
        case 0:
            default_logger->set_level(spdlog::level::off);
            break;
        case 1:
            default_logger->set_level(spdlog::level::critical);
            break;
        case 2:
            default_logger->set_level(spdlog::level::err);
            break;
        case 3:
            default_logger->set_level(spdlog::level::warn);
            break;
        case 4:
            default_logger->set_level(spdlog::level::info);
            break;
        case 5:
            default_logger->set_level(spdlog::level::debug);
            break;
        default: {
            default_logger->set_level(spdlog::level::info);
            break;
        }
    }

    // Register the stdout logger
    spdlog::register_logger(default_logger);

    // Set as the default logger
    spdlog::set_default_logger(default_logger);

    if (!no_log_file) {
        spdlog::info("Writing to the log file: {}", log_fullname);
    }

    if (log_performance) {
        // Set the performance console
        auto perf_console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        perf_console_sink->set_pattern(PERF_PATTERN);

        // Set performance sinks
        std::vector<spdlog::sink_ptr> perf_sinks;
        perf_sinks.push_back(perf_console_sink);

        // Set a log file with its full name, maximum size and the number of rotated files
        std::string perf_log_fullname;
        if (!no_log_file) {
            perf_log_fullname = (fs::path(getenv("HOME")) / CARTA_USER_FOLDER_PREFIX / "log/performance.log").string();
            auto perf_log_file_sink =
                std::make_shared<spdlog::sinks::rotating_file_sink_mt>(perf_log_fullname, LOG_FILE_SIZE, ROTATED_LOG_FILES);
            perf_log_file_sink->set_pattern(PERF_PATTERN);
            perf_sinks.push_back(perf_log_file_sink);
        }

        // Create the performance logger
        auto perf_logger = std::make_shared<spdlog::logger>(PERF_TAG, std::begin(perf_sinks), std::end(perf_sinks));

        // Set the performance logger level same with the stdout logger
        perf_logger->set_level(default_logger->level());

        // Register the performance logger
        spdlog::register_logger(perf_logger);
    }

    spdlog::flush_every(std::chrono::seconds(3));
}

void LogReceivedEventType(const CARTA::EventType& event_type) {
    if (log_protocol_messages) {
        auto event_name = CARTA::EventType_Name(CARTA::EventType(event_type));
        if (!event_name.empty()) {
            spdlog::debug("[protocol] <== {}", event_name);
        } else {
            spdlog::debug("[protocol] <== unknown event type: {}!", event_type);
        }
    }
}

void LogSentEventType(const CARTA::EventType& event_type) {
    if (log_protocol_messages) {
        auto event_name = CARTA::EventType_Name(CARTA::EventType(event_type));
        if (!event_name.empty()) {
            spdlog::debug("[protocol] ==> {}", event_name);
        } else {
            spdlog::debug("[protocol] ==> unknown event type: {}!", event_type);
        }
    }
}

void FlushLogFile() {
    spdlog::default_logger()->flush();
    if (spdlog::get(PERF_TAG)) {
        spdlog::get(PERF_TAG)->flush();
    }
}
