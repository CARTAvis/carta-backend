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
    // Test for 0.5 ms accuracy
    static constexpr double timer_eps = 0.5;
    static constexpr int delay_millis = 25;
};

TEST_F(TimerTest, RecordTime) {
    Timer t;
    t.Start("RecordTime");
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_millis));
    t.End("RecordTime");
    auto dt = t.GetMeasurement("RecordTime").count();
    EXPECT_GT(dt, 0);
}

TEST_F(TimerTest, IgnoresWrongOrder) {
    Timer t;
    t.End("IgnoresWrongOrder");
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_millis));
    t.End("IgnoresWrongOrder");
    auto dt = t.GetMeasurement("IgnoresWrongOrder").count();
    EXPECT_LT(dt, 0);
}

TEST_F(TimerTest, AccurateAverage) {
    Timer t;

    for (auto i = 0; i < 5; i++) {
        t.Start("AccurateAverage");
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_millis));
        t.End("AccurateAverage");
    }

    auto dt = t.GetMeasurement("AccurateAverage").count();
    auto diff = dt - delay_millis;
    EXPECT_LT(diff, timer_eps);
}

TEST_F(TimerTest, AccurateTime) {
    Timer t;
    t.Start("AccurateTime");
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_millis));
    t.End("AccurateTime");
    auto dt = t.GetMeasurement("AccurateTime").count();
    auto diff = dt - delay_millis;
    EXPECT_LT(diff, timer_eps);
}

TEST_F(TimerTest, ClearWorks) {
    Timer t;
    t.Start("ClearWorks");
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_millis));
    t.End("ClearWorks");
    t.Clear("ClearWorks");
    auto dt = t.GetMeasurement("ClearWorks").count();
    EXPECT_LT(dt, 0);
}

TEST_F(TimerTest, ClearAllWorks) {
    Timer t;
    t.Start("ClearAllWorks");
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_millis));
    t.End("ClearAllWorks");
    t.Clear();
    auto dt = t.GetMeasurement("ClearAllWorks").count();
    EXPECT_LT(dt, 0);
}

TEST_F(TimerTest, MeasurementStringWorks) {
    Timer t;
    t.Start("MeasurementStringWorks");
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_millis));
    t.End("MeasurementStringWorks");
    EXPECT_EQ(t.GetMeasurementString("MeasurementStringWorks").rfind("MeasurementStringWorks: 25."), 0);

    // Clear after fetching
    t.GetMeasurementString("MeasurementStringWorks", true);
    auto dt = t.GetMeasurement("MeasurementStringWorks").count();
    EXPECT_LT(dt, 0);
}