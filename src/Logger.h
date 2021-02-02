/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

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

enum class LogType { DEBUG, INFO, WARN, ERROR };

void CreateLoggers(bool no_log_file, bool debug_log);

template <typename S, typename... Args>
void SpdLog(const std::string& log_tag, const LogType& log_type, bool flush_now, const S& format, Args&&... args) {
    std::shared_ptr<spdlog::logger> logger = spdlog::get(log_tag);
    if (logger) {
        switch (log_type) {
            case LogType::DEBUG:
                logger->debug(fmt::format(format, args...));
                break;
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
    } else {
        spdlog::critical("Fail to get the logger: {}!", STDOUT_TAG);
    }
}

template <typename S, typename... Args>
void DEBUG(const S& format, Args&&... args) {
    SpdLog(STDOUT_TAG, LogType::DEBUG, true, format, args...);
}

template <typename S, typename... Args>
void INFO(const S& format, Args&&... args) {
    SpdLog(STDOUT_TAG, LogType::INFO, true, format, args...);
}

template <typename S, typename... Args>
void WARN(const S& format, Args&&... args) {
    SpdLog(STDOUT_TAG, LogType::WARN, true, format, args...);
}

template <typename S, typename... Args>
void ERROR(const S& format, Args&&... args) {
    SpdLog(STDOUT_TAG, LogType::ERROR, true, format, args...);
}

#endif // CARTA_BACKEND__LOGGER_H_
