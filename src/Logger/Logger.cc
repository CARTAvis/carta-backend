/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Logger.h"

#include "Main/ProgramSettings.h"

#include <fstream>
#include <functional>
#include <nlohmann/json.hpp>
#include <regex>
#include <string>

namespace carta {
namespace logger {

static bool log_protocol_messages(false);

using LogAction = std::function<void(const std::string& name, uint64_t& count, const std::string& dir)>;

static std::unordered_map<CARTA::EventType, LogAction> registry;
static std::mutex registry_mutex;
static std::unordered_map<CARTA::EventType, uint64_t> inbound_counts;
static std::unordered_map<CARTA::EventType, uint64_t> outbound_counts;

LogAction createNoOpLogger() {
    return [](const std::string& name, uint64_t& count, const std::string& dir) {};
}

LogAction createNormalLogger() {
    return [](const std::string& name, uint64_t& count, const std::string& arrow) { spdlog::debug("[protocol] {} {}", arrow, name); };
}

LogAction createThrottledLogger(uint32_t first_n, uint32_t every_m) {
    return [first_n, every_m](const std::string& name, uint64_t& count, const std::string& dir) {
        if (count <= first_n || count % every_m == 0) {
            std::string suffix = (count > first_n) ? fmt::format(" (Total: {})", count) : "";
            spdlog::debug("[protocol] {} {}{}", dir, name, suffix);
        }
    };
}

void BuildRegistry() {
    auto& settings = ProgramSettings::GetInstance();
    auto* descriptor = CARTA::EventType_descriptor();

    nlohmann::json config_data;

    if (!settings.log_config_path.empty()) {
        std::ifstream inputFile(settings.log_config_path);
        if (inputFile.is_open()) {
            try {
                inputFile >> config_data;
                spdlog::info("Successfully loaded custom config from: {}", settings.log_config_path);
            } catch (const nlohmann::json::parse_error& e) {
                spdlog::info("Error parsing JSON file: {}", e.what());
            }
        } else {
            spdlog::debug("Could not open file: {}", settings.log_config_path);
        }
    }

    if (config_data.contains("rules") && config_data["rules"].is_array()) {
        for (int i = 0; i < descriptor->value_count(); ++i) {
            auto* value = descriptor->value(i);
            auto event_type = static_cast<CARTA::EventType>(value->number());
            std::string name = value->name();

            bool matched = false;

            for (const auto& rule : config_data["rules"]) {
                std::regex pattern(rule.value("match", ""));

                if (std::regex_match(name, pattern)) {
                    std::string action = rule.value("action", "normal");

                    if (action == "throttle") {
                        registry[event_type] = createThrottledLogger(rule.value("first_n", 5), rule.value("every_m", 100));
                    } else if (action == "none") {
                        registry[event_type] = createNoOpLogger();
                    }

                    matched = true;
                    break;
                }
            }

            if (!matched) {
                registry[event_type] = createNormalLogger();
            }
        }
    }
}

void InitLogger() {
    BuildRegistry();

    // Copy parameters from the global settings
    auto& settings = ProgramSettings::GetInstance();
    log_protocol_messages = settings.log_protocol_messages;
    auto no_log = settings.no_log;
    auto user_directory = settings.user_directory;
    auto verbosity = settings.verbosity;
    auto log_performance = settings.log_performance;

    // Set the stdout/stderr console
    auto console_sink = std::make_shared<spdlog::sinks::carta_sink>();
    console_sink->set_pattern(CARTA_LOGGER_PATTERN);

    // Set stdout sinks
    std::vector<spdlog::sink_ptr> console_sinks;
    console_sinks.push_back(console_sink);

    // Set a log file with its full name, maximum size and the number of rotated files
    std::string log_fullname;
    if (!no_log) {
        log_fullname = (user_directory / "log/carta.log").string();
        auto stdout_log_file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(log_fullname, LOG_FILE_SIZE, ROTATED_LOG_FILES);
        stdout_log_file_sink->set_formatter(
            std::make_unique<spdlog::pattern_formatter>(CARTA_FILE_LOGGER_PATTERN, spdlog::pattern_time_type::utc));
        console_sinks.push_back(stdout_log_file_sink);
    }

    // Create the stdout logger
    auto default_logger = std::make_shared<spdlog::logger>(CARTA_LOGGER_TAG, std::begin(console_sinks), std::end(console_sinks));

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

    if (!no_log) {
        spdlog::info("Writing to the log file: {}", log_fullname);
    }

    if (log_performance) {
        // Set the performance console
        auto perf_console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        perf_console_sink->set_formatter(std::make_unique<spdlog::pattern_formatter>(PERF_PATTERN, spdlog::pattern_time_type::utc));

        // Set performance sinks
        std::vector<spdlog::sink_ptr> perf_sinks;
        perf_sinks.push_back(perf_console_sink);

        // Set a log file with its full name, maximum size and the number of rotated files
        std::string perf_log_fullname;
        if (!no_log) {
            perf_log_fullname = (user_directory / "log/performance.log").string();
            auto perf_log_file_sink =
                std::make_shared<spdlog::sinks::rotating_file_sink_mt>(perf_log_fullname, LOG_FILE_SIZE, ROTATED_LOG_FILES);
            perf_log_file_sink->set_formatter(std::make_unique<spdlog::pattern_formatter>(PERF_PATTERN, spdlog::pattern_time_type::utc));
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

void ExecuteLog(CARTA::EventType type, uint64_t& count, const std::string& arrow) {
    std::lock_guard<std::mutex> lock(registry_mutex);

    count++; // Increments the specific map passed in

    auto it = registry.find(type);
    if (it != registry.end()) {
        it->second(CARTA::EventType_Name(type), count, arrow);
    }
}

void LogReceivedEventType(const CARTA::EventType& event_type) {
    if (!log_protocol_messages)
        return;
    ExecuteLog(event_type, inbound_counts[event_type], "<==");
}

void LogSentEventType(const CARTA::EventType& event_type) {
    if (!log_protocol_messages)
        return;
    ExecuteLog(event_type, outbound_counts[event_type], "==>");
}

void FlushLogFile() {
    spdlog::default_logger()->flush();
    if (spdlog::get(PERF_TAG)) {
        spdlog::get(PERF_TAG)->flush();
    }
}

} // namespace logger
} // namespace carta
