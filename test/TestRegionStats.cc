/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include <cmath>

#include "CommonTestUtilities.h"
#include "ImageData/FileLoader.h"
#include "Region/Region.h"
#include "Region/RegionHandler.h"
#include "src/Frame/Frame.h"

using namespace carta;

class RegionStatsTest : public ::testing::Test {
public:
    static CARTA::SetStatsRequirements SetStatsRequirements(int32_t file_id, int32_t region_id) {
        CARTA::SetStatsRequirements set_stats_requirements;
        set_stats_requirements.set_file_id(file_id);
        set_stats_requirements.set_region_id(region_id);
        auto* stats_config = set_stats_requirements.add_stats_configs();
        stats_config->add_stats_types(CARTA::StatsType::NumPixels);
        stats_config->add_stats_types(CARTA::StatsType::Sum);
        stats_config->add_stats_types(CARTA::StatsType::Mean);
        stats_config->add_stats_types(CARTA::StatsType::RMS);
        stats_config->add_stats_types(CARTA::StatsType::Sigma);
        stats_config->add_stats_types(CARTA::StatsType::SumSq);
        stats_config->add_stats_types(CARTA::StatsType::Min);
        stats_config->add_stats_types(CARTA::StatsType::Max);
        stats_config->add_stats_types(CARTA::StatsType::Median);
        stats_config->add_stats_types(CARTA::StatsType::MedAbsDevMed);
        stats_config->add_stats_types(CARTA::StatsType::Quartile);
        stats_config->add_stats_types(CARTA::StatsType::Q1);
        stats_config->add_stats_types(CARTA::StatsType::Q3);
        return set_stats_requirements;
    }

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
        auto loader = carta::FileLoader::GetLoader(image_path);
        std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));
        carta::RegionHandler region_handler;

        // Set polygon region
        int file_id(0), region_id(-1);
        auto csys = frame->CoordinateSystem();
        if (!SetRegion(region_handler, file_id, region_id, endpoints, csys, is_annotation)) {
            return false;
        }

        // Set stats requirements
        auto stats_req_message = SetStatsRequirements(file_id, region_id);
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
    auto image_path = FitsImages() / "noise_3d.fits";
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
    std::sort(image_data.begin(), image_data.end());
    double expected_median = (image_data[7] + image_data[8]) / 2.0;
    double expected_q1 = image_data[3];
    double expected_q3 = image_data[11];
    std::vector<double> absolute_deviations;
    std::transform(image_data.begin(), image_data.end(), std::back_inserter(absolute_deviations),
        [expected_median](double value) { return std::abs(value - expected_median); });
    std::sort(absolute_deviations.begin(), absolute_deviations.end());
    double expected_mad = (absolute_deviations[7] + absolute_deviations[8]) / 2.0;

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
        } else if (stats_data.statistics(i).stats_type() == CARTA::StatsType::Median) {
            ASSERT_NEAR(stats_data.statistics(i).value(), expected_median, 1e-7);
        } else if (stats_data.statistics(i).stats_type() == CARTA::StatsType::MedAbsDevMed) {
            ASSERT_NEAR(stats_data.statistics(i).value(), expected_mad, 1e-7);
        } else if (stats_data.statistics(i).stats_type() == CARTA::StatsType::Quartile) {
            ASSERT_NEAR(stats_data.statistics(i).value(), expected_q3 - expected_q1, 1e-7);
        } else if (stats_data.statistics(i).stats_type() == CARTA::StatsType::Q1) {
            ASSERT_NEAR(stats_data.statistics(i).value(), expected_q1, 1e-7);
        } else if (stats_data.statistics(i).stats_type() == CARTA::StatsType::Q3) {
            ASSERT_NEAR(stats_data.statistics(i).value(), expected_q3, 1e-7);
        }
    }
}

TEST_F(RegionStatsTest, TestFitsImageStatsRecalculateMissingStatistics) {
    auto image_path = FitsImages() / "noise_3d.fits";
    auto loader = carta::FileLoader::GetLoader(image_path);
    std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));

    CARTA::SetStatsRequirements_StatsConfig basic_config;
    basic_config.add_stats_types(CARTA::StatsType::Mean);
    ASSERT_TRUE(frame->SetStatsRequirements(IMAGE_REGION_ID, {basic_config}));
    ASSERT_TRUE(frame->FillRegionStatsData([](CARTA::RegionStatsData) {}, IMAGE_REGION_ID, 0));

    auto stats_req_message = SetStatsRequirements(0, IMAGE_REGION_ID);
    std::vector<CARTA::SetStatsRequirements_StatsConfig> stats_configs = {
        stats_req_message.stats_configs().begin(), stats_req_message.stats_configs().end()};
    ASSERT_TRUE(frame->SetStatsRequirements(IMAGE_REGION_ID, stats_configs));

    CARTA::RegionStatsData stats_data;
    ASSERT_TRUE(frame->FillRegionStatsData([&stats_data](CARTA::RegionStatsData data) { stats_data = data; }, IMAGE_REGION_ID, 0));

    int robust_stats_count = 0;
    for (const auto& statistic : stats_data.statistics()) {
        switch (statistic.stats_type()) {
            case CARTA::StatsType::Median:
            case CARTA::StatsType::MedAbsDevMed:
            case CARTA::StatsType::Quartile:
            case CARTA::StatsType::Q1:
            case CARTA::StatsType::Q3:
                ASSERT_TRUE(std::isfinite(statistic.value()));
                ++robust_stats_count;
                break;
            default:
                break;
        }
    }
    ASSERT_EQ(robust_stats_count, 5);
}

TEST_F(RegionStatsTest, TestHdf5ImageStatsRecalculateMissingStatistics) {
    auto image_path = Hdf5Images() / "noise_10px_10px.hdf5";
    auto loader = carta::FileLoader::GetLoader(image_path);
    std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));

    CARTA::SetStatsRequirements_StatsConfig basic_config;
    basic_config.add_stats_types(CARTA::StatsType::Mean);
    ASSERT_TRUE(frame->SetStatsRequirements(IMAGE_REGION_ID, {basic_config}));
    ASSERT_TRUE(frame->FillRegionStatsData([](CARTA::RegionStatsData) {}, IMAGE_REGION_ID, 0));

    auto stats_req_message = SetStatsRequirements(0, IMAGE_REGION_ID);
    std::vector<CARTA::SetStatsRequirements_StatsConfig> stats_configs = {
        stats_req_message.stats_configs().begin(), stats_req_message.stats_configs().end()};
    ASSERT_TRUE(frame->SetStatsRequirements(IMAGE_REGION_ID, stats_configs));

    CARTA::RegionStatsData stats_data;
    ASSERT_TRUE(frame->FillRegionStatsData([&stats_data](CARTA::RegionStatsData data) { stats_data = data; }, IMAGE_REGION_ID, 0));

    int robust_stats_count = 0;
    for (const auto& statistic : stats_data.statistics()) {
        switch (statistic.stats_type()) {
            case CARTA::StatsType::Median:
            case CARTA::StatsType::MedAbsDevMed:
            case CARTA::StatsType::Quartile:
            case CARTA::StatsType::Q1:
            case CARTA::StatsType::Q3:
                ASSERT_TRUE(std::isfinite(statistic.value()));
                ++robust_stats_count;
                break;
            default:
                break;
        }
    }
    ASSERT_EQ(robust_stats_count, 5);
}

TEST_F(RegionStatsTest, TestFitsAnnotationRegionStats) {
    auto image_path = FitsImages() / "noise_3d.fits";
    std::vector<float> endpoints = {0.0, 0.0, 0.0, 3.0, 3.0, 3.0, 3.0, 0.0};
    CARTA::RegionStatsData stats_data;
    bool ok = RegionStats(image_path, endpoints, stats_data, true);
    ASSERT_FALSE(ok);
}
