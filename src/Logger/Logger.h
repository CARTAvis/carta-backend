/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_LOGGER_LOGGER_H_
#define CARTA_BACKEND_LOGGER_LOGGER_H_

#include <iostream>

#include <spdlog/common.h>
#include <spdlog/details/log_msg.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <carta-protobuf/enums.pb.h>

#include "Util/FileSystem.h"

#define LOG_FILE_SIZE 1024 * 1024 * 5 // (Bytes)
#define ROTATED_LOG_FILES 5
#define CARTA_LOGGER_TAG "CARTA"
#define CARTA_LOGGER_PATTERN "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v"
#define PERF_TAG "performance"
#define PERF_PATTERN "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v"

// customize the log function for performance
namespace spdlog {
constexpr auto performance = [](auto&&... args) {
    auto perf_log = spdlog::get(PERF_TAG);
    if (perf_log) {
        perf_log->debug(std::forward<decltype(args)>(args)...);
    }
};

namespace sinks {
/*
   Adapted from spdlog:
   - https://github.com/gabime/spdlog/issues/345
   - https://github.com/gabime/spdlog/blob/v1.x/include/spdlog/sinks/ansicolor_sink.h
   - https://github.com/gabime/spdlog/blob/v1.x/include/spdlog/sinks/ansicolor_sink-inl.h
   - spdlog is MIT license: https://github.com/gabime/spdlog/blob/v1.x/LICENSE
*/
class carta_sink : public ansicolor_sink<details::console_mutex> {
public:
    carta_sink()
        : ansicolor_sink<details::console_mutex>(stdout, color_mode::automatic),
          mutex_(details::console_mutex::mutex()),
          formatter_(details::make_unique<spdlog::pattern_formatter>()) {
        target_file_ = stdout;
        colors_[level::trace] = to_string_(white);
        colors_[level::debug] = to_string_(cyan);
        colors_[level::info] = to_string_(green);
        colors_[level::warn] = to_string_(yellow_bold);
        colors_[level::err] = to_string_(red_bold);
        colors_[level::critical] = to_string_(bold_on_red);
        colors_[level::off] = to_string_(reset);
    }

    void log(const details::log_msg& msg) override {
        std::lock_guard<mutex_t> lock(mutex_);
        msg.color_range_start = 0;
        msg.color_range_end = 0;
        memory_buf_t formatted;
        formatter_->format(msg, formatted);
        target_file_ = msg.level < SPDLOG_LEVEL_ERROR ? stdout : stderr; // floating output
        if (msg.color_range_end > msg.color_range_start) {
            // before color range
            print_range_(formatted, 0, msg.color_range_start);
            // in color range
            print_ccode_(colors_[msg.level]);
            print_range_(formatted, msg.color_range_start, msg.color_range_end);
            print_ccode_(reset);
            // after color range
            print_range_(formatted, msg.color_range_end, formatted.size());
        } else // no color
        {
            print_range_(formatted, 0, formatted.size());
        }
        fflush(target_file_);
    }

    void flush() override {
        std::lock_guard<mutex_t> lock(mutex_);
        fflush(target_file_);
    }

private:
    FILE* target_file_;
    mutex_t& mutex_;
    std::unique_ptr<spdlog::formatter> formatter_;
    std::array<std::string, level::n_levels> colors_;

    inline void print_range_(const memory_buf_t& formatted, size_t start, size_t end) {
        fwrite(formatted.data() + start, sizeof(char), end - start, target_file_);
    }
    inline void print_ccode_(const string_view_t& color_code) {
        fwrite(color_code.data(), sizeof(char), color_code.size(), target_file_);
    }
    inline std::string to_string_(const string_view_t& sv) {
        return std::string(sv.data(), sv.size());
    }
};
} // namespace sinks
} // namespace spdlog

namespace carta {
namespace logger {
void InitLogger(bool no_log_file, int verbosity, bool log_performance, bool log_protocol_messages_, fs::path user_directory);
void LogReceivedEventType(const CARTA::EventType& event_type);
void LogSentEventType(const CARTA::EventType& event_type);
void FlushLogFile();
} // namespace logger
} // namespace carta

#endif // CARTA_BACKEND_LOGGER_LOGGER_H_
