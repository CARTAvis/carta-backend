/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_UTIL_REPORTCADENCE_H_
#define CARTA_SRC_UTIL_REPORTCADENCE_H_

#include <chrono>
#include <functional>

namespace carta {

// How often a long calculation tells the frontend how far it has got: no more than once an interval,
// and its final report whatever the interval says.
//
// A report is due once more than the interval has passed since the last one that went out, or since
// the cadence was made; one that goes out starts the interval again. An interval of zero makes every
// report due. A report a caller sends whatever the cadence says -- the one between the two halves of a
// cube histogram -- is sent without asking, and so leaves the interval running.
//
// The clock is a monotonic one, so that the wall clock being set back does not silence the reports
// until it catches up. It is a parameter for this class's own tests; everything else takes the default.
class ReportCadence {
public:
    using Clock = std::chrono::steady_clock;

    explicit ReportCadence(std::chrono::milliseconds interval, std::function<Clock::time_point()> now = Clock::now);

    // Whether a report of `progress` goes out now: the interval has passed, or the progress is final.
    bool Due(float progress);

    // The same for a caller whose final report goes out some other way, so that only the interval
    // decides.
    bool Due();

private:
    bool DueIf(bool final);

    std::chrono::milliseconds _interval;
    std::function<Clock::time_point()> _now;
    Clock::time_point _last;
};

} // namespace carta

#endif // CARTA_SRC_UTIL_REPORTCADENCE_H_
