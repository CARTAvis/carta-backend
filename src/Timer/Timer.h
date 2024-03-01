/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2024 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_TIMER_TIMER_H_
#define CARTA_SRC_TIMER_TIMER_H_

#include <chrono>
#include <string>
#include <unordered_map>

namespace carta {

struct TimeDelta {
    double microseconds;

    double ms() const {
        return (microseconds / 1000.0);
    }
    double us() const {
        return microseconds;
    }
};

class Timer {
public:
    Timer();
    ~Timer() = default;

    TimeDelta Elapsed();

private:
    std::chrono::high_resolution_clock::time_point _t_start;
};

} // namespace carta

#endif // CARTA_SRC_TIMER_TIMER_H_
