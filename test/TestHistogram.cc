/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <cmath> // for NAN
#include <random>
#include <vector>

#include <fmt/format.h>
#include <gtest/gtest.h>

#include "ImageStats/Histogram.h"
#include "Threading.h"
#include "Timer/Timer.h"

using namespace std;
random_device rd;
mt19937 mt(rd());
uniform_real_distribution<float> float_random(0, 1.0f);

bool CompareResults(const carta::HistogramResults& a, const carta::HistogramResults& b) {
    if (a.num_bins != b.num_bins || a.bin_center != b.bin_center || a.bin_width != b.bin_width) {
        return false;
    }

    for (auto i = 0; i < a.num_bins; i++) {
        auto bin_a = a.histogram_bins[i];
        auto bin_b = b.histogram_bins[i];
        if (bin_a != bin_b) {
            return false;
        }
    }

    return true;
}

carta::Histogram CalcHistogram(int nbins, float min, float max, const std::vector<float>& data) {
    if (false) {
        return carta::Histogram(1, 0, 0, {});
    } else {
        return carta::Histogram(nbins, min, max, data);
    }
}

TEST(Histogram, TestHistogramUsage) {
    std::vector<float> data(1024 * 1024);
    std::for_each(data.begin(), data.end(), [](float& v) { v = float_random(mt); });
    carta::Histogram hist = CalcHistogram(1024, 0, 1, data);
    EXPECT_TRUE(hist.GetNbins() == 1024) << "Wrong number of bins";
    EXPECT_TRUE(hist.GetMinVal() == 0) << "Wrong min value";
    EXPECT_TRUE(hist.GetMaxVal() == 1) << "Wrong max value";
}

TEST(Histogram, TestHistogramBehaviour) {
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
    carta::Histogram hist(10, 0.0f, 10.0f, data);
    auto results = hist.GetHistogram();
    auto counts = accumulate(results.histogram_bins.begin(), results.histogram_bins.end(), 0);
    EXPECT_TRUE(counts == 12);
    EXPECT_TRUE(results.histogram_bins[0] == 2);
    EXPECT_TRUE(results.histogram_bins[1] == 1);
    EXPECT_TRUE(results.histogram_bins[4] == 4);
    EXPECT_TRUE(results.histogram_bins[5] == 3);
    EXPECT_TRUE(results.histogram_bins[9] == 2);

    data.clear();
    data.push_back(NAN);
    data.push_back(NAN);
    data.push_back(NAN);
    carta::Histogram hist2(10, 0.0f, 10.0f, data);
    // expect 0 counts
    auto bins = hist2.GetHistogramBins();
    EXPECT_TRUE(accumulate(bins.begin(), bins.end(), 0) == 0);
}

TEST(Histogram, TestHistogramConstructor) {
    std::vector<float> data(1024 * 1024);
    std::for_each(data.begin(), data.end(), [](float& v) { v = float_random(mt); });
    carta::Histogram hist(1024, 0.0f, 1.0f, data);
    auto results = hist.GetHistogram();

    carta::Histogram hist2(hist);
    auto results2 = hist2.GetHistogram();

    EXPECT_TRUE(CompareResults(results, results2));
}

TEST(Histogram, TestHistogramJoin) {
    std::vector<float> data(1024 * 1024);
    std::for_each(data.begin(), data.end(), [](float& v) { v = float_random(mt); });
    carta::Histogram hist(1024, 0.0f, 1.0f, data);
    auto results = hist.GetHistogram();
    const auto total_counts = accumulate(results.histogram_bins.begin(), results.histogram_bins.end(), 0);

    carta::Histogram hist2(1024, 0.0f, 1.0f, data);
    auto results2 = hist2.GetHistogram();

    EXPECT_TRUE(CompareResults(results, results2)); // naive?

    hist.join(hist2);
    results = hist.GetHistogram();
    const auto total_counts2 = accumulate(results.histogram_bins.begin(), results.histogram_bins.end(), 0);

    EXPECT_TRUE(total_counts * 2 == total_counts2);

    carta::Histogram hist3(512, 0.0f, 1.0f, data);
    EXPECT_FALSE(hist.join(hist3));
}

TEST(Histogram, TestSingleThreading) {
    std::vector<float> data(1024 * 1024);
    for (auto& v : data) {
        v = float_random(mt);
    }

    carta::ThreadManager::SetThreadLimit(1);
    carta::Histogram hist_st(1024, 0.0f, 1.0f, data);
    auto results_st = hist_st.GetHistogram();

    for (auto i = 2; i < 24; i++) {
        carta::Histogram hist_mt(1024, 0.0f, 1.0f, data);
        auto results_mt = hist_mt.GetHistogram();
        EXPECT_TRUE(CompareResults(results_st, results_mt));
    }
}

TEST(Histogram, TestMultithreading) {
    std::vector<float> data(1024 * 1024);
    for (auto& v : data) {
        v = float_random(mt);
    }

    carta::ThreadManager::SetThreadLimit(1);
    carta::Histogram hist_st(1024, 0.0f, 1.0f, data);
    auto results_st = hist_st.GetHistogram();

    for (auto i = 2; i < 24; i++) {
        carta::ThreadManager::SetThreadLimit(i);
        carta::Histogram hist_mt(1024, 0.0f, 1.0f, data);
        auto results_mt = hist_mt.GetHistogram();
        EXPECT_TRUE(CompareResults(results_st, results_mt));
    }
}

TEST(Histogram, TestMultithreadingPerformance) {
    std::vector<float> data(1024 * 1024);
    for (auto& v : data) {
        v = float_random(mt);
    }

    Timer t;
    carta::ThreadManager::SetThreadLimit(1);

    t.Start("single_threaded");
    carta::Histogram hist_st(1024, 0.0f, 1.0f, data);
    auto results_st = hist_st.GetHistogram();
    t.End("single_threaded");

    carta::ThreadManager::SetThreadLimit(4);
    t.Start("multi_threaded");
    carta::Histogram hist_mt(1024, 0.0f, 1.0f, data);
    auto results_mt = hist_mt.GetHistogram();
    t.End("multi_threaded");

    auto st_time = t.GetMeasurement("single_threaded");
    auto mt_time = t.GetMeasurement("multi_threaded");
    double speedup = st_time / mt_time;
    EXPECT_GE(speedup, 1.5) << "Speedup is: " << speedup;
}
