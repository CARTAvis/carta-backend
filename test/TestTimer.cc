/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <chrono>
#include <thread>

#include <fmt/format.h>
#include <gtest/gtest.h>

#include "../Timer/Timer.h"

using namespace std;

// Test for 0.5 ms accuracy
double timer_eps = 0.5;
int delay_millis = 25;

TEST(Timer, RecordTime) {
    Timer t;
    t.Start("RecordTime");
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_millis));
    t.End("RecordTime");
    auto dt = t.GetMeasurement("RecordTime").count();
    EXPECT_GT(dt, 0);
}

TEST(Timer, IgnoresWrongOrder) {
    Timer t;
    t.End("IgnoresWrongOrder");
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_millis));
    t.End("IgnoresWrongOrder");
    auto dt = t.GetMeasurement("IgnoresWrongOrder").count();
    EXPECT_LT(dt, 0);
}

TEST(Timer, AccurateAverage) {
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

TEST(Timer, AccurateTime) {
    Timer t;
    t.Start("AccurateTime");
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_millis));
    t.End("AccurateTime");
    auto dt = t.GetMeasurement("AccurateTime").count();
    auto diff = dt - delay_millis;
    EXPECT_LT(diff, timer_eps);
}

TEST(Timer, ClearWorks) {
    Timer t;
    t.Start("ClearWorks");
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_millis));
    t.End("ClearWorks");
    t.Clear("ClearWorks");
    auto dt = t.GetMeasurement("ClearWorks").count();
    EXPECT_LT(dt, 0);
}

TEST(Timer, ClearAllWorks) {
    Timer t;
    t.Start("ClearAllWorks");
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_millis));
    t.End("ClearAllWorks");
    t.Clear();
    auto dt = t.GetMeasurement("ClearAllWorks").count();
    EXPECT_LT(dt, 0);
}

TEST(Timer, MeasurementStringWorks) {
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

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
