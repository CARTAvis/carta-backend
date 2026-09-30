/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ReportCadence.h"

#include <utility>

namespace carta {

ReportCadence::ReportCadence(std::chrono::milliseconds interval, std::function<Clock::time_point()> now)
    : _interval(interval), _now(std::move(now)), _last(_now()) {}

bool ReportCadence::Due(float progress) {
    return DueIf(progress >= 1.0F);
}

bool ReportCadence::Due() {
    return DueIf(false);
}

bool ReportCadence::DueIf(bool final) {
    const auto now = _now();
    if (final || _interval.count() == 0 || (now - _last) > _interval) {
        _last = now;
        return true;
    }
    return false;
}

} // namespace carta
