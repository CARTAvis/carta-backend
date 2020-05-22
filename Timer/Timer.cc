#include <iostream>
#include "Timer.h"

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

timer_duration Timer::GetMeasurement(const std::string& timer_name) const {
    auto itr_measurements = _measurements.find(timer_name);
    if (itr_measurements != _measurements.end()) {
        auto total_count = itr_measurements->second.second;
        auto total_time = itr_measurements->second.first;
        return total_time / total_count;
    }
    return timer_duration(-1);
}

std::string Timer::GetMeasurementString(const std::string& timer_name) const {
    if (timer_name.empty()) {
        return "";
    }
    auto itr = _measurements.find(timer_name);
    if (itr == _measurements.end()) {
        return fmt::format("{}: No Measurements found", timer_name);
    }
    return fmt::format("{}: {:.2f} ms ({} count{})", timer_name, itr->second.first.count(), itr->second.second, itr->second.second != 1 ? "s" : "");
}

void Timer::Print(const std::string& timer_name) const {
    std::string output;
    if (timer_name.empty()) {
        for (auto& m: _measurements) {
            output += GetMeasurementString(m.first) + "\n";
        }
    } else {
        output = GetMeasurementString(timer_name) + "\n";
    }
    std::cout << output;
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
