/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_LOGGER_LOGGER_H_
#define CARTA_BACKEND_LOGGER_LOGGER_H_

#include <fmt/format.h>

#define SPDLOG_FMT_EXTERNAL
#define FMT_HEADER_ONLY

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "Constants.h"

enum class LogType { DEBUG, INFO, WARN, ERROR };

void InitLoggers(bool no_log_file, bool debug_log, bool perf_log);

template <typename S, typename... Args>
void SpdLog(const std::string& log_tag, const LogType& log_type, bool flush_now, const S& format, Args&&... args);

template <typename S, typename... Args>
void DEBUG(const S& format, Args&&... args);

template <typename S, typename... Args>
void INFO(const S& format, Args&&... args);

template <typename S, typename... Args>
void WARN(const S& format, Args&&... args);

template <typename S, typename... Args>
void ERROR(const S& format, Args&&... args);

template <typename S, typename... Args>
void PERF(const S& format, Args&&... args);

#include "Logger.tcc"

#endif // CARTA_BACKEND_LOGGER_LOGGER_H_
