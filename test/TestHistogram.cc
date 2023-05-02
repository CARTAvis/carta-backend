/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "CommonTestUtilities.h"
#include "ImageStats/Histogram.h"
#include "ThreadingManager/ThreadingManager.h"

#ifdef COMPILE_PERFORMANCE_TESTS
#include <spdlog/fmt/fmt.h>
#include "Timer/Timer.h"
#endif

class HistogramTest : public ::testing::Test {
public:
    std::random_device rd;
    std::mt19937 mt;
    std::uniform_real_distribution<float> float_random;

    HistogramTest() {
        mt = std::mt19937(rd());
        float_random = std::uniform_real_distribution<float>(0, 1.0f);
    }
};

TEST_F(HistogramTest, TestHistogramBehaviour) {
    std::vector<float> data;
    // test histogram filling
    data.push_back(0.0);  // should go to bin at pos. 0 - first bin is closed from below
    data.push_back(0.5);  // should go to bin at pos. 0
    data.push_back(1.0);  // should go to bin at pos. 1 - middle bins are semi-open, just closed from below
    data.push_back(4.0);  // should go to bin at pos. 4
    data.push_back(4.5);  // should go to bin at pos. 4
    data.push_back(4.7);  // should go to bin at pos. 4
    data.push_back(4.9);  // should go to bin at pos. 4
    data.push_back(5.0);  // should go to bin at pos. 5
    data.push_back(5.0);  // should go to bin at pos. 5
    data.push_back(5.0);  // should go to bin at pos. 5
    data.push_back(9.1);  // should go to bin at pos. 9
    data.push_back(10.0); // should go to bin at pos. 9 - last bin is closed from above
    // values that will fall in the overflow and underflow range
    data.push_back(-1.0);        // should not appear
    data.push_back(00.0 - 1e-9); // should not appear
    data.push_back(10.0 + 1e+9); // should not appear
    data.push_back(11.0);        // should not appear
    carta::Histogram hist(10, HistogramBounds(0.0, 10.0), data.data(), data.size());
    auto counts = accumulate(hist.GetHistogramBins().begin(), hist.GetHistogramBins().end(), 0);
    EXPECT_EQ(counts, 12);
    EXPECT_EQ(hist.GetHistogramBins()[0], 2);
    EXPECT_EQ(hist.GetHistogramBins()[1], 1);
    EXPECT_EQ(hist.GetHistogramBins()[9], 2);
    EXPECT_EQ(hist.GetHistogramBins()[4], 4);
    EXPECT_EQ(hist.GetHistogramBins()[5], 3);

    data.clear();
    data.push_back(NAN);
    data.push_back(NAN);
    data.push_back(NAN);
    carta::Histogram hist2(10, HistogramBounds(0.0, 10.0), data.data(), data.size());
    // expect 0 counts
    auto bins = hist2.GetHistogramBins();
    EXPECT_EQ(accumulate(bins.begin(), bins.end(), 0), 0);
}

TEST_F(HistogramTest, TestHistogramConstructor) {
    std::vector<float> data(1024 * 1024);
    std::for_each(data.begin(), data.end(), [&](float& v) { v = float_random(mt); });
    carta::Histogram hist(1024, HistogramBounds(0.0, 1.0), data.data(), data.size());
    carta::Histogram hist2(hist);
    EXPECT_TRUE(CmpHistograms(hist, hist2));
}

TEST_F(HistogramTest, TestHistogramAdd) {
    std::vector<float> data(1024 * 1024);
    std::for_each(data.begin(), data.end(), [&](float& v) { v = float_random(mt); });
    carta::Histogram hist(1024, HistogramBounds(0.0, 1.0), data.data(), data.size());
    const auto total_counts = accumulate(hist.GetHistogramBins().begin(), hist.GetHistogramBins().end(), 0);
    carta::Histogram hist2(1024, HistogramBounds(0.0, 1.0), data.data(), data.size());
    EXPECT_TRUE(CmpHistograms(hist, hist2)); // naive?
    hist.Add(hist2);
    const auto total_counts2 = accumulate(hist.GetHistogramBins().begin(), hist.GetHistogramBins().end(), 0);
    EXPECT_EQ(2 * total_counts, total_counts2);
    carta::Histogram hist3(512, HistogramBounds(0.0, 1.0), data.data(), data.size());
    EXPECT_FALSE(hist.Add(hist3));
}

TEST_F(HistogramTest, TestSingleThreading) {
    std::vector<float> data(1024 * 1024);
    for (auto& v : data) {
        v = float_random(mt);
    }
    carta::ThreadManager::SetThreadLimit(1);
    carta::Histogram hist_st(1024, HistogramBounds(0.0, 1.0), data.data(), data.size());
    for (auto i = 2; i < 24; i++) {
        carta::Histogram hist_mt(1024, HistogramBounds(0.0, 1.0), data.data(), data.size());
        EXPECT_TRUE(CmpHistograms(hist_st, hist_mt));
    }
}

TEST_F(HistogramTest, TestMultithreading) {
    std::vector<float> data(1024 * 1024);
    for (auto& v : data) {
        v = float_random(mt);
    }

    carta::ThreadManager::SetThreadLimit(1);
    carta::Histogram hist_st(1024, HistogramBounds(0.0, 1.0), data.data(), data.size());
    for (auto i = 2; i < 24; i++) {
        carta::ThreadManager::SetThreadLimit(i);
        carta::Histogram hist_mt(1024, HistogramBounds(0.0, 1.0), data.data(), data.size());
        EXPECT_TRUE(CmpHistograms(hist_st, hist_mt));
    }
}
#ifdef COMPILE_PERFORMANCE_TESTS

TEST_F(HistogramTest, TestMultithreadingPerformance) {
    std::vector<float> data(1024 * 1024);
    for (auto& v : data) {
        v = float_random(mt);
    }

    carta::Timer t;
    carta::ThreadManager::SetThreadLimit(1);

    t.Start("single_threaded");
    carta::Histogram hist_st(1024, 0.0f, 1.0f, data.data(), data.size());
    t.End("single_threaded");

    carta::ThreadManager::SetThreadLimit(4);
    t.Start("multi_threaded");
    carta::Histogram hist_mt(1024, 0.0f, 1.0f, data.data(), data.size());
    t.End("multi_threaded");

    auto st_time = t.GetMeasurement("single_threaded");
    auto mt_time = t.GetMeasurement("multi_threaded");
    double speedup = st_time / mt_time;
    EXPECT_GE(speedup, 1.5) << "Speedup is: " << speedup;
}

#endif
