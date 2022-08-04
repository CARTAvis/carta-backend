/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Timer.h"

using namespace carta;

Timer::Timer() : _t_start(std::chrono::high_resolution_clock::now()), _stop(false) {}

double Timer::Elapsed(Unit unit) {
    if (!_stop) {
        _t_end = std::chrono::high_resolution_clock::now();
        _stop = true;
    }
    if (unit == ms) {
        return std::chrono::duration_cast<std::chrono::microseconds>(_t_end - _t_start).count() / 1000.0;
    }
    return std::chrono::duration_cast<std::chrono::microseconds>(_t_end - _t_start).count();
}
