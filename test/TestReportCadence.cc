/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <chrono>

#include <gtest/gtest.h>

#include "Util/ReportCadence.h"

using namespace carta;
using namespace std::chrono_literals;

namespace {

// A clock that moves only when the test moves it.
struct ManualClock {
    ReportCadence::Clock::time_point now{};

    std::function<ReportCadence::Clock::time_point()> Reader() {
        return [this]() { return now; };
    }
};

} // namespace

TEST(ReportCadenceTest, AReportIsDueOnceMoreThanTheIntervalHasPassed) {
    ManualClock clock;
    ReportCadence cadence(500ms, clock.Reader());
    EXPECT_FALSE(cadence.Due(0.1F)) << "the interval runs from when the cadence was made";
    clock.now += 500ms;
    EXPECT_FALSE(cadence.Due(0.2F)) << "exactly the interval is not more than it";
    clock.now += 1ms;
    EXPECT_TRUE(cadence.Due(0.3F));
}

TEST(ReportCadenceTest, AReportThatGoesOutStartsTheIntervalAgain) {
    ManualClock clock;
    ReportCadence cadence(500ms, clock.Reader());
    clock.now += 600ms;
    ASSERT_TRUE(cadence.Due(0.1F));
    clock.now += 400ms;
    EXPECT_FALSE(cadence.Due(0.2F)) << "400ms since the last report, though 1s since the start";
    clock.now += 101ms;
    EXPECT_TRUE(cadence.Due(0.3F));
}

TEST(ReportCadenceTest, TheFinalReportIsDueWhateverTheInterval) {
    ManualClock clock;
    ReportCadence cadence(500ms, clock.Reader());
    EXPECT_TRUE(cadence.Due(1.0F));
    EXPECT_TRUE(cadence.Due(1.0F));
}

TEST(ReportCadenceTest, TheFinalReportStartsTheIntervalAgainToo) {
    ManualClock clock;
    ReportCadence cadence(500ms, clock.Reader());
    clock.now += 300ms;
    ASSERT_TRUE(cadence.Due(1.0F));
    clock.now += 300ms;
    EXPECT_FALSE(cadence.Due(0.5F)) << "300ms since the final report, though 600ms since the start";
}

// For a caller that sends its final report some other way: progress does not come into it.
TEST(ReportCadenceTest, WithoutProgressOnlyTheIntervalDecides) {
    ManualClock clock;
    ReportCadence cadence(500ms, clock.Reader());
    EXPECT_FALSE(cadence.Due());
    clock.now += 501ms;
    EXPECT_TRUE(cadence.Due());
    EXPECT_FALSE(cadence.Due());
}

// A report sent without asking -- a milestone -- does not start the interval again.
TEST(ReportCadenceTest, AReportSentWithoutAskingLeavesTheIntervalRunning) {
    ManualClock clock;
    ReportCadence cadence(500ms, clock.Reader());
    clock.now += 400ms;
    // The milestone goes out here, and the cadence is not asked.
    clock.now += 101ms;
    EXPECT_TRUE(cadence.Due()) << "501ms since the start, whatever went out at 400ms";
}

TEST(ReportCadenceTest, AnIntervalOfZeroMakesEveryReportDue) {
    ManualClock clock;
    ReportCadence cadence(0ms, clock.Reader());
    EXPECT_TRUE(cadence.Due(0.1F)) << "even with a clock that has not moved";
    EXPECT_TRUE(cadence.Due(0.2F));
    EXPECT_TRUE(cadence.Due());
}

// The clock is monotonic: a report is not held back by the wall clock being set back.
TEST(ReportCadenceTest, TheDefaultClockIsMonotonic) {
    EXPECT_TRUE(ReportCadence::Clock::is_steady);
}
