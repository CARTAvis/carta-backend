/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

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

TEST(Histogram, TestHistogramBehaviour) {
    std::vector<float> data;
    // test histogram filling
    data.push_back(0.0); // should go to bin at pos. 0 - first bin is closed from below
    data.push_back(0.5); // should go to bin at pos. 0
    data.push_back(1.0); // should go to bin at pos. 1 - middle bins are semi-open, just closed from below
    data.push_back(4.0); // should go to bin at pos. 4
    data.push_back(4.5); // should go to bin at pos. 4
    data.push_back(4.7); // should go to bin at pos. 4
    data.push_back(4.9); // should go to bin at pos. 4
    data.push_back(5.0); // should go to bin at pos. 5
    data.push_back(5.0); // should go to bin at pos. 5
    data.push_back(5.0); // should go to bin at pos. 5
    data.push_back(9.1); // should go to bin at pos. 9
    data.push_back(10.0); // should go to bin at pos. 9 - last bin is closed from above
    // values that will fall in the overflow and underflow range
    data.push_back(-1.0); // should not appear
    data.push_back(00.0 - 1e-9); // should not appear
    data.push_back(10.0 + 1e+9); // should not appear
    data.push_back(11.0); // should not appear
    carta::Histogram hist(10, 0.0f, 10.0f, data);
    hist.setup_bins();
    auto results = hist.GetHistogram();
    auto counts = accumulate(results.histogram_bins.begin(), results.histogram_bins.end(), 0);
    EXPECT_TRUE(counts == 12);
    EXPECT_TRUE(results.histogram_bins[0] == 2);
    EXPECT_TRUE(results.histogram_bins[1] == 1);
    EXPECT_TRUE(results.histogram_bins[4] == 4);
    EXPECT_TRUE(results.histogram_bins[5] == 3);
    EXPECT_TRUE(results.histogram_bins[9] == 2);
}

TEST(Histogram, TestHistogramConstructor) {
    std::vector<float> data(1024 * 1024);
    std::for_each(data.begin(), data.end(), [](float &v) { v = float_random(mt); });
    carta::Histogram hist(1024, 0.0f, 1.0f, data);
    hist.setup_bins();
    auto results  = hist.GetHistogram();

    carta::Histogram hist2(hist);
    auto results2 = hist2.GetHistogram();
    
    EXPECT_TRUE(CompareResults(results, results2));
}

TEST(Histogram, TestHistogramJoin) {
    std::vector<float> data(1024 * 1024);
    std::for_each(data.begin(), data.end(), [](float &v) { v = float_random(mt); });
    carta::Histogram hist(1024, 0.0f, 1.0f, data);
    hist.setup_bins();
    auto results  = hist.GetHistogram();
    const auto total_counts = accumulate(results.histogram_bins.begin(), results.histogram_bins.end(), 0);

    carta::Histogram hist2(1024, 0.0f, 1.0f, data);
    hist2.setup_bins();
    auto results2  = hist2.GetHistogram();

    EXPECT_TRUE(CompareResults(results, results2)); // naive?
    
    hist.join(hist2);
    results = hist.GetHistogram();
    const auto total_counts2 = accumulate(results.histogram_bins.begin(), results.histogram_bins.end(), 0);

    EXPECT_TRUE(total_counts * 2 == total_counts2);
}

TEST(Histogram, TestSingleThreading) {
    std::vector<float> data(1024 * 1024);
    for (auto& v : data) {
        v = float_random(mt);
    }

    carta::ThreadManager::SetThreadLimit(1);
    carta::Histogram hist_st(1024, 0.0f, 1.0f, data);
    hist_st.setup_bins();
    auto results_st = hist_st.GetHistogram();

    for (auto i = 2; i < 24; i++) {
        carta::Histogram hist_mt(1024, 0.0f, 1.0f, data);
        hist_mt.setup_bins();
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
    hist_st.setup_bins();
    auto results_st = hist_st.GetHistogram();

    for (auto i = 2; i < 24; i++) {
        carta::ThreadManager::SetThreadLimit(i);
        carta::Histogram hist_mt(1024, 0.0f, 1.0f, data);
        hist_mt.setup_bins();
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
    hist_st.setup_bins();
    auto results_st = hist_st.GetHistogram();
    t.End("single_threaded");

    carta::ThreadManager::SetThreadLimit(4);
    t.Start("multi_threaded");
    carta::Histogram hist_mt(1024, 0.0f, 1.0f, data);
    hist_mt.setup_bins();
    auto results_mt = hist_mt.GetHistogram();
    t.End("multi_threaded");

    auto st_time = t.GetMeasurement("single_threaded");
    auto mt_time = t.GetMeasurement("multi_threaded");
    double speedup = st_time / mt_time;
    cout << speedup << endl;
    EXPECT_GE(speedup, 1.5);
}
