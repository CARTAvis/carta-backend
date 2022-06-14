/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021, 2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_TIMER_TIMER_H_
#define CARTA_BACKEND_TIMER_TIMER_H_

#include <chrono>
#include <string>
#include <unordered_map>

typedef std::chrono::time_point<std::chrono::high_resolution_clock> timer_entry;
typedef std::chrono::duration<double, std::milli> timer_duration;

namespace carta {

class Timer {
public:
    void Start(const std::string& timer_name);
    void End(const std::string& timer_name);
    void Clear(const std::string& timer_name = "");
    timer_duration GetMeasurement(const std::string& timer_name, bool clear_after_fetch = false);
    std::string GetMeasurementString(const std::string& timer_name, bool clear_after_fetch = false);
    void Print(const std::string& timer_name = "", bool clear_after_fetch = false);

protected:
    std::unordered_map<std::string, timer_entry> _entries;
    std::unordered_map<std::string, std::pair<timer_duration, int>> _measurements;
};

} // namespace carta

#endif // CARTA_BACKEND_TIMER_TIMER_H_
