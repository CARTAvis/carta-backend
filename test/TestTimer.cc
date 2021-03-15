/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "../src/Timer/Timer.h"

using namespace std;

class TimerTest : public ::testing::Test {
public:
    // Test for 0.1 ms accuracy
    static constexpr double timer_eps = 0.1;
    static constexpr double delay_millis = 2.5;

    void block_for_millis(double millis) {
        double dt = 0;
        auto t_start = chrono::high_resolution_clock::now();
        while (dt < millis) {
            auto now = chrono::high_resolution_clock::now();
            dt = chrono::duration_cast<chrono::microseconds>(now - t_start).count() / 1000.0;
        }
    }
};

TEST_F(TimerTest, RecordTime) {
    Timer t;
    t.Start("RecordTime");
    block_for_millis(delay_millis);
    t.End("RecordTime");
    auto dt = t.GetMeasurement("RecordTime").count();
    EXPECT_GT(dt, 0);
}

TEST_F(TimerTest, IgnoresWrongOrder) {
    Timer t;
    t.End("IgnoresWrongOrder");
    block_for_millis(delay_millis);
    t.End("IgnoresWrongOrder");
    auto dt = t.GetMeasurement("IgnoresWrongOrder").count();
    EXPECT_LT(dt, 0);
}

TEST_F(TimerTest, AccurateAverage) {
    Timer t;

    for (auto i = 0; i < 5; i++) {
        t.Start("AccurateAverage");
        block_for_millis(delay_millis);
        t.End("AccurateAverage");
    }

    auto dt = t.GetMeasurement("AccurateAverage").count();
    auto diff = dt - delay_millis;
    EXPECT_LT(diff, timer_eps);
}

TEST_F(TimerTest, AccurateTime) {
    Timer t;
    t.Start("AccurateTime");
    block_for_millis(delay_millis);
    t.End("AccurateTime");
    auto dt = t.GetMeasurement("AccurateTime").count();
    auto diff = dt - delay_millis;
    EXPECT_LT(diff, timer_eps);
}

TEST_F(TimerTest, ClearWorks) {
    Timer t;
    t.Start("ClearWorks");
    block_for_millis(delay_millis);
    t.End("ClearWorks");
    t.Clear("ClearWorks");
    auto dt = t.GetMeasurement("ClearWorks").count();
    EXPECT_LT(dt, 0);
}

TEST_F(TimerTest, ClearAllWorks) {
    Timer t;
    t.Start("ClearAllWorks");
    block_for_millis(delay_millis);
    t.End("ClearAllWorks");
    t.Clear();
    auto dt = t.GetMeasurement("ClearAllWorks").count();
    EXPECT_LT(dt, 0);
}

TEST_F(TimerTest, MeasurementStringWorks) {
    Timer t;
    t.Start("MeasurementStringWorks");
    block_for_millis(delay_millis);
    t.End("MeasurementStringWorks");
    EXPECT_EQ(t.GetMeasurementString("MeasurementStringWorks").rfind("MeasurementStringWorks: 2.5"), 0);

    // Clear after fetching
    t.GetMeasurementString("MeasurementStringWorks", true);
    auto dt = t.GetMeasurement("MeasurementStringWorks").count();
    EXPECT_LT(dt, 0);
}