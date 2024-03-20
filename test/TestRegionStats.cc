/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include "CommonTestUtilities.h"
#include "ImageData/FileLoader.h"
#include "Region/Region.h"
#include "Region/RegionHandler.h"
#include "src/Frame/Frame.h"

using namespace carta;

class RegionStatsTest : public ::testing::Test {
public:
    static bool SetRegion(carta::RegionHandler& region_handler, int file_id, int& region_id, const std::vector<float>& points,
        std::shared_ptr<casacore::CoordinateSystem> csys, bool is_annotation) {
        std::vector<CARTA::Point> control_points;
        for (auto i = 0; i < points.size(); i += 2) {
            control_points.push_back(Message::Point(points[i], points[i + 1]));
        }

        // Define RegionState for line region and set region (region_id updated)
        auto npoints(control_points.size());
        CARTA::RegionType region_type = CARTA::RegionType::POLYGON;
        if (is_annotation) {
            region_type = CARTA::RegionType::ANNPOLYGON;
        }
        RegionState region_state(file_id, region_type, control_points, 0.0);
        return region_handler.SetRegion(region_id, region_state, csys);
    }

    static bool RegionStats(const std::string& image_path, const std::vector<float>& endpoints, CARTA::RegionStatsData& region_stats,
        bool is_annotation = false) {
        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(image_path));
        std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));
        carta::RegionHandler region_handler;

        // Set polygon region
        int file_id(0), region_id(-1);
        auto csys = frame->CoordinateSystem();
        if (!SetRegion(region_handler, file_id, region_id, endpoints, csys, is_annotation)) {
            return false;
        }

        // Set stats requirements
        auto stats_req_message = Message::SetStatsRequirements(file_id, region_id);
        std::vector<CARTA::SetStatsRequirements_StatsConfig> stats_configs = {
            stats_req_message.stats_configs().begin(), stats_req_message.stats_configs().end()};
        if (!region_handler.SetStatsRequirements(region_id, file_id, frame, stats_configs)) {
            return false;
        }

        // Get stats
        return region_handler.FillRegionStatsData(
            [&](CARTA::RegionStatsData stats_data) { region_stats = stats_data; }, region_id, file_id);
    }
};

TEST_F(RegionStatsTest, TestFitsRegionStats) {
    std::string image_path = FileFinder::FitsImagePath("noise_3d.fits");
    std::vector<float> endpoints = {1.0, 1.0, 1.0, 4.0, 4.0, 4.0, 4.0, 1.0};
    CARTA::RegionStatsData stats_data;
    bool ok = RegionStats(image_path, endpoints, stats_data);

    // Check stats fields
    ASSERT_TRUE(ok);
    ASSERT_EQ(stats_data.file_id(), 0);
    ASSERT_EQ(stats_data.region_id(), 1);
    ASSERT_EQ(stats_data.channel(), 0);
    ASSERT_EQ(stats_data.stokes(), 0);
    ASSERT_GT(stats_data.statistics_size(), 0);

    // Calc expected stats from image data
    FitsDataReader reader(image_path);
    auto image_data = reader.ReadRegion({1, 1, 0}, {5, 5, 1});
    double expected_sum = std::accumulate(image_data.begin(), image_data.end(), 0.0);
    double expected_mean = expected_sum / image_data.size();
    double expected_min = *std::min_element(image_data.begin(), image_data.end());
    double expected_max = *std::max_element(image_data.begin(), image_data.end());

    // Check some stats
    for (size_t i = 0; i < stats_data.statistics_size(); ++i) {
        if (stats_data.statistics(i).stats_type() == CARTA::StatsType::NumPixels) {
            ASSERT_DOUBLE_EQ(stats_data.statistics(i).value(), (double)image_data.size());
        } else if (stats_data.statistics(i).stats_type() == CARTA::StatsType::Sum) {
            ASSERT_DOUBLE_EQ(stats_data.statistics(i).value(), expected_sum);
        } else if (stats_data.statistics(i).stats_type() == CARTA::StatsType::Mean) {
            ASSERT_DOUBLE_EQ(stats_data.statistics(i).value(), expected_mean);
        } else if (stats_data.statistics(i).stats_type() == CARTA::StatsType::Min) {
            ASSERT_DOUBLE_EQ(stats_data.statistics(i).value(), expected_min);
        } else if (stats_data.statistics(i).stats_type() == CARTA::StatsType::Max) {
            ASSERT_DOUBLE_EQ(stats_data.statistics(i).value(), expected_max);
        }
    }
}

TEST_F(RegionStatsTest, TestFitsAnnotationRegionStats) {
    std::string image_path = FileFinder::FitsImagePath("noise_3d.fits");
    std::vector<float> endpoints = {0.0, 0.0, 0.0, 3.0, 3.0, 3.0, 3.0, 0.0};
    CARTA::RegionStatsData stats_data;
    bool ok = RegionStats(image_path, endpoints, stats_data, true);
    ASSERT_FALSE(ok);
}
