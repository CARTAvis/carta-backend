/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
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
    enum Unit { ms, us };

    Timer();
    ~Timer() = default;

    double Elapsed(Unit unit);

private:
    std::chrono::high_resolution_clock::time_point _t_start;
    std::chrono::high_resolution_clock::time_point _t_end;
    bool _stop;
};

} // namespace carta

#endif // CARTA_BACKEND_TIMER_TIMER_H_
