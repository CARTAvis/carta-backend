/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_LOGGER_LOGGER_H_
#define CARTA_BACKEND_LOGGER_LOGGER_H_

#include <spdlog/spdlog.h>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <carta-protobuf/enums.pb.h>

#include "Constants.h"

// customize the log function for performance
namespace spdlog {
constexpr auto performance = [](auto&&... args) {
    auto perf_log = spdlog::get(PERF_TAG);
    if (perf_log) {
        perf_log->debug(std::forward<decltype(args)>(args)...);
    }
};
} // namespace spdlog

void InitLogger(bool no_log_file, int verbosity, bool log_performance, bool log_protocol_messages_);

void LogReceivedEventType(const CARTA::EventType& event_type);

void LogSentEventType(const CARTA::EventType& event_type);

void FlushLogFile();

#endif // CARTA_BACKEND_LOGGER_LOGGER_H_
