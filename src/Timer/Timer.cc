/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021, 2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Timer.h"

#include <spdlog/fmt/fmt.h>

#include "Logger/Logger.h"

using namespace carta;

void Timer::Start(const std::string& timer_name) {
    if (!timer_name.empty()) {
        auto t_start = std::chrono::high_resolution_clock::now();
        _entries[timer_name] = t_start;
    }
}

void Timer::End(const std::string& timer_name) {
    auto t_stop = std::chrono::high_resolution_clock::now();
    if (!timer_name.empty()) {
        auto itr_entries = _entries.find(timer_name);
        if (itr_entries != _entries.end()) {
            auto t_start = itr_entries->second;
            timer_duration dt(t_stop - t_start);
            _entries.erase(itr_entries);
            auto itr_measurements = _measurements.find(timer_name);
            if (itr_measurements != _measurements.end()) {
                itr_measurements->second.first += dt;
                itr_measurements->second.second += 1;
            } else {
                _measurements[timer_name] = {dt, 1};
            }
        }
    }
}

timer_duration Timer::GetMeasurement(const std::string& timer_name, bool clear_after_fetch) {
    auto itr_measurements = _measurements.find(timer_name);
    if (itr_measurements != _measurements.end()) {
        auto total_count = itr_measurements->second.second;
        auto total_time = itr_measurements->second.first;
        if (clear_after_fetch) {
            Clear(timer_name);
        }
        return total_time / total_count;
    }
    return timer_duration(-1);
}

std::string Timer::GetMeasurementString(const std::string& timer_name, bool clear_after_fetch) {
    if (timer_name.empty()) {
        return "";
    }
    auto itr = _measurements.find(timer_name);
    if (itr == _measurements.end()) {
        return fmt::format("{}: No Measurements found", timer_name);
    }
    auto str = fmt::format(
        "{}: {:.2f} ms ({} count{})", timer_name, itr->second.first.count(), itr->second.second, itr->second.second != 1 ? "s" : "");
    if (clear_after_fetch) {
        Clear(timer_name);
    }
    return str;
}

void Timer::Print(const std::string& timer_name, bool clear_after_fetch) {
    std::string output;
    if (timer_name.empty()) {
        for (auto& m : _measurements) {
            output += GetMeasurementString(m.first, clear_after_fetch) + "\n";
        }
    } else {
        output = GetMeasurementString(timer_name, clear_after_fetch) + "\n";
    }
    spdlog::info(output);
}
void Timer::Clear(const std::string& timer_name) {
    // Clear all entries if no name passed in
    if (timer_name.empty()) {
        _entries.clear();
        _measurements.clear();
    } else {
        _entries.erase(timer_name);
        _measurements.erase(timer_name);
    }
}
