#ifndef CARTA_BACKEND__LOGGER_H_
#define CARTA_BACKEND__LOGGER_H_

#include <spdlog/cfg/env.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <filesystem>

namespace fs = std::filesystem;

void InitLoggers() {
    // Set a log file name and its path
    std::string log_filename = fs::path(getenv("HOME")).string() + "/CARTA/log/carta.log";
    spdlog::info("Set the log file name {0}", log_filename);

    // Set a log file with its maximum size 5 MB and with 0 rotated file
    auto log_file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(log_filename, 1048576 * 5, 0);

    // Set a log file pattern
    log_file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] %v");

    // Create a logger (tag RECEIVE) and save its messages to a log file
    auto receive_logger = std::make_shared<spdlog::logger>("RECEIVE", log_file_sink);
    receive_logger->info("\">>>>>>>>> Start the RECEIVE logger <<<<<<<<<\"");

    // Register the logger (tag RECEIVE) so we can get it globally
    spdlog::register_logger(receive_logger);

    // Create a logger (tag SEND) and save its messages to a log file
    auto send_logger = std::make_shared<spdlog::logger>("SEND", log_file_sink);
    send_logger->info("\">>>>>>>>> Start the SEND logger <<<<<<<<<\"");

    // Register the logger (tag SEND) so we can get it globally
    spdlog::register_logger(send_logger);
}

#endif // CARTA_BACKEND__LOGGER_H_