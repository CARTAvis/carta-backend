#ifndef CARTA_BACKEND__LOGGER_H_
#define CARTA_BACKEND__LOGGER_H_

#include <fmt/format.h>

#define SPDLOG_FMT_EXTERNAL
#define FMT_HEADER_ONLY

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <filesystem>

#include "Constants.h"

namespace fs = std::filesystem;

enum class LogType { INFO, WARN, ERROR };

void CreateLogger(const bool& no_log_file);

template <typename S, typename... Args>
void SpdLog(const LogType& log_type, bool flush_now, const S& format, Args&&... args) {
    std::shared_ptr<spdlog::logger> logger = spdlog::get(LOG_TAG);
    if (!logger) {
        spdlog::critical("Fail to get the logger: {}!", LOG_TAG);
    } else {
        switch (log_type) {
            case LogType::INFO:
                logger->info(fmt::format(format, args...));
                break;
            case LogType::WARN:
                logger->warn(fmt::format(format, args...));
                break;
            default: {
                logger->error(fmt::format(format, args...));
                break;
            }
        }
        if (flush_now) {
            logger->flush();
        }
    }
}

template <typename S, typename... Args>
void INFO(const S& format, Args&&... args) {
    SpdLog(LogType::INFO, true, format, args...);
}

template <typename S, typename... Args>
void WARN(const S& format, Args&&... args) {
    SpdLog(LogType::WARN, true, format, args...);
}

template <typename S, typename... Args>
void ERROR(const S& format, Args&&... args) {
    SpdLog(LogType::ERROR, true, format, args...);
}

#endif // CARTA_BACKEND__LOGGER_H_
