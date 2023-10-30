/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/
#include <gtest/gtest.h>

#include <map>
#include <random>
#include <unordered_map>

#include <carta-protobuf/enums.pb.h>

#include "Logger/Logger.h"
#include "Timer/Timer.h"

using namespace std;

class MapPerfTest : public ::testing::Test {};

TEST_F(MapPerfTest, CmpMapAndUnorderedMap) {
    map<CARTA::StatsType, vector<double>> stats_map;
    unordered_map<CARTA::StatsType, vector<double>> stats_unordered_map;

    // Generate random numbers
    int num(1000000);
    vector<double> random_data(num);

    double lower_bound(0);
    double upper_bound(100);
    uniform_real_distribution<double> unif(lower_bound, upper_bound);
    default_random_engine re;

    for (int i = 0; i < num; ++i) {
        random_data[i] = unif(re);
    }

    // Test the time spent for inserting data into the map/unordered_map
    vector<CARTA::StatsType> stats_types{CARTA::StatsType::Sum, CARTA::StatsType::Extrema, CARTA::StatsType::FluxDensity,
        CARTA::StatsType::Max, CARTA::StatsType::Mean, CARTA::StatsType::Min, CARTA::StatsType::RMS, CARTA::StatsType::Sigma,
        CARTA::StatsType::SumSq, CARTA::StatsType::Blc, CARTA::StatsType::Trc, CARTA::StatsType::NumPixels, CARTA::StatsType::MaxPos,
        CARTA::StatsType::MinPos};

    carta::Timer t;
    for (auto stats_type : stats_types) {
        // stats_map[stats_type] = random_data;
        stats_map.emplace(stats_type, random_data);
    }
    auto dt1 = t.Elapsed().us();
    fmt::print("Elapsed time for inserting the data into a map: {:.0f} us.\n", dt1);

    t.Restart();
    for (auto stats_type : stats_types) {
        // stats_unordered_map[stats_type] = random_data;
        stats_unordered_map.emplace(stats_type, random_data);
    }
    auto dt2 = t.Elapsed().us();
    fmt::print("Elapsed time for inserting the data into an unordered_map: {:.0f} us.\n", dt2);

    fmt::print("Elapsed time ratio inserting the data into an unordered_map/map: {:.2f}\n", dt2 / dt1);

    // Test the time spent for finding and copying the data from the map/unordered_map
    vector<double> copied_data;

    t.Restart();
    for (auto stats_type : stats_types) {
        auto it = stats_map.find(stats_type);
        if (it == stats_map.end()) {
            cerr << fmt::format("Stats type {} not found from the map!\n");
        } else {
            copied_data = it->second;
        }
    }
    auto dt3 = t.Elapsed().us();
    fmt::print("Elapsed time for finding and copying the data from a map: {:.0f} us.\n", dt3);

    t.Restart();
    for (auto stats_type : stats_types) {
        auto it = stats_unordered_map.find(stats_type);
        if (it == stats_unordered_map.end()) {
            cerr << fmt::format("Stats type {} not found from the unordered_map!\n");
        } else {
            copied_data = it->second;
        }
    }
    auto dt4 = t.Elapsed().us();
    fmt::print("Elapsed time for finding and copying the data from an unordered_map: {:.0f} us.\n", dt4);

    fmt::print("Elapsed time ratio for finding and copying the data from a unordered_map/map: {:.2}\n", dt4 / dt3);

    // Test the time spent for finding and erasing the data from the map/unordered_map
    t.Restart();
    for (auto stats_type : stats_types) {
        auto it = stats_map.find(stats_type);
        if (it == stats_map.end()) {
            cerr << fmt::format("Stats type {} not found from the map!\n");
        } else {
            stats_map.erase(it);
        }
    }
    auto dt5 = t.Elapsed().us();
    fmt::print("Elapsed time for finding and erasing the data from a map: {:.0f} us.\n", dt5);

    t.Restart();
    for (auto stats_type : stats_types) {
        auto it = stats_unordered_map.find(stats_type);
        if (it == stats_unordered_map.end()) {
            cerr << fmt::format("Stats type {} not found from the unordered_map!\n");
        } else {
            stats_unordered_map.erase(it);
        }
    }
    auto dt6 = t.Elapsed().us();
    fmt::print("Elapsed time for finding and erasing the data from an unordered_map: {:.0f} us.\n", dt6);

    fmt::print("Elapsed time ratio for finding and erasing the data from a unordered_map/map: {:.2f}\n", dt6 / dt5);
}
