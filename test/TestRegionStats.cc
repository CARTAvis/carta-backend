/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include <casacore/coordinates/Coordinates/CoordinateUtil.h>
#include <casacore/images/Images/TempImage.h>
#include <cmath>
#include <thread>

#include "CommonTestUtilities.h"
#include "ImageData/FileLoader.h"
#include "ImageStats/StatsCalculator.h"
#include "Region/Region.h"
#include "Region/RegionHandler.h"
#include "Session/Session.h"
#include "src/Frame/Frame.h"

using namespace carta;

class StatsCacheFrame : public Frame {
public:
    using Frame::_image_stats;
    using Frame::Frame;
};

class StatsQueueSession : public Session {
public:
    StatsQueueSession(std::shared_ptr<Frame> frame) : Session(nullptr, nullptr, 0, "", nullptr) {
        _frames[0] = frame;
    }
    using Session::_out_msgs;
};

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

TEST_F(RegionStatsTest, TestRapidStatisticsRequestsProcessedInArrivalOrder) {
    auto loader = FileLoader::GetLoader(FitsImages() / "noise_3d.fits");
    auto frame = std::make_shared<StatsCacheFrame>(0, loader, "0");
    StatsQueueSession session(frame);
    CARTA::SetStatsRequirements request;
    request.set_file_id(0);
    request.set_region_id(IMAGE_REGION_ID);
    auto config = request.add_stats_configs();
    for (auto type : {CARTA::StatsType::Median, CARTA::StatsType::Q1, CARTA::StatsType::Q3, CARTA::StatsType::Quartile,
             CARTA::StatsType::MedAbsDevMed}) {
        config->add_stats_types(type);
        session.QueueStatsRequirements(request);
    }
    std::vector<std::thread> workers;
    for (int i = 0; i < 5; ++i) {
        workers.emplace_back([&session]() { session.ExecuteStatsRequirements(); });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    ASSERT_EQ(session._out_msgs.size(), 5);
    for (int count = 1; count <= 5; ++count) {
        std::pair<std::vector<char>, bool> encoded;
        ASSERT_TRUE(session._out_msgs.try_pop(encoded));
        const auto& bytes = encoded.first;
        auto response = Message::DecodeMessage<CARTA::RegionStatsData>(std::string_view(bytes.data(), bytes.size()));
        ASSERT_EQ(response.statistics_size(), count);
        for (const auto& statistic : response.statistics()) {
            EXPECT_TRUE(std::isfinite(statistic.value()));
        }
    }
    ASSERT_EQ(frame->_image_stats.size(), 1);
    EXPECT_EQ(frame->_image_stats.begin()->second.size(), 5);
}

TEST_F(RegionStatsTest, TestQuantilesPerChannelIgnoreInvalidPixels) {
    casacore::TempImage<float> image(casacore::TiledShape(casacore::IPosition(3, 2, 2, 2)), casacore::CoordinateUtil::defaultCoords3D());
    casacore::Array<float> pixels(image.shape(), FLOAT_NAN);
    pixels(casacore::IPosition(3, 0, 0, 0)) = 1.0;
    pixels(casacore::IPosition(3, 1, 0, 0)) = 2.0;
    pixels(casacore::IPosition(3, 0, 1, 0)) = 3.0;
    image.put(pixels);
    std::map<CARTA::StatsType, std::vector<double>> results;
    const std::vector<CARTA::StatsType> robust_stats = {CARTA::StatsType::Median, CARTA::StatsType::MedAbsDevMed,
        CARTA::StatsType::Q1, CARTA::StatsType::Q3, CARTA::StatsType::Quartile};
    ASSERT_TRUE(CalcStatsValues(results, robust_stats, image, true));
    ASSERT_EQ(results.size(), robust_stats.size());
    for (const auto& [type, values] : results) {
        ASSERT_EQ(values.size(), 2);
        EXPECT_TRUE(std::isnan(values[1]));
    }
    EXPECT_DOUBLE_EQ(results.at(CARTA::StatsType::Median)[0], 2.0);
    EXPECT_DOUBLE_EQ(results.at(CARTA::StatsType::Q1)[0], 1.0);
    EXPECT_DOUBLE_EQ(results.at(CARTA::StatsType::Q3)[0], 3.0);
    EXPECT_DOUBLE_EQ(results.at(CARTA::StatsType::Quartile)[0], 2.0);
    EXPECT_DOUBLE_EQ(results.at(CARTA::StatsType::MedAbsDevMed)[0], 1.0);

    results.clear();
    ASSERT_TRUE(CalcStatsValues(results, robust_stats, image, false));
    EXPECT_DOUBLE_EQ(results.at(CARTA::StatsType::Median)[0], 2.0);
    EXPECT_DOUBLE_EQ(results.at(CARTA::StatsType::Q1)[0], 1.0);
    EXPECT_DOUBLE_EQ(results.at(CARTA::StatsType::Q3)[0], 3.0);
    EXPECT_DOUBLE_EQ(results.at(CARTA::StatsType::Quartile)[0], 2.0);
    EXPECT_DOUBLE_EQ(results.at(CARTA::StatsType::MedAbsDevMed)[0], 1.0);
}

TEST_F(RegionStatsTest, TestImageQuantilesCachedTogether) {
    auto loader = FileLoader::GetLoader(FitsImages() / "noise_3d.fits");
    StatsCacheFrame frame(0, loader, "0");
    CARTA::SetStatsRequirements_StatsConfig config;
    config.add_stats_types(CARTA::StatsType::Median);
    ASSERT_TRUE(frame.SetStatsRequirements(IMAGE_REGION_ID, {config}));
    CARTA::RegionStatsData data;
    auto receive = [&data](CARTA::RegionStatsData response) { data = response; };
    ASSERT_TRUE(frame.FillRegionStatsData(receive, IMAGE_REGION_ID, 0));
    ASSERT_EQ(data.statistics_size(), 1);
    ASSERT_EQ(frame._image_stats.size(), 1);
    auto& cache = frame._image_stats.begin()->second;
    for (auto type : {CARTA::StatsType::Median, CARTA::StatsType::Q1, CARTA::StatsType::Q3, CARTA::StatsType::Quartile}) {
        ASSERT_TRUE(cache.count(type));
        EXPECT_TRUE(std::isfinite(cache.at(type)));
    }
    ASSERT_FALSE(cache.count(CARTA::StatsType::MedAbsDevMed));
    // Mark a cached value to prove that subsequent requests do not recalculate it.
    cache[CARTA::StatsType::Q1] = -12345.0;
    config.add_stats_types(CARTA::StatsType::Q1);
    ASSERT_TRUE(frame.SetStatsRequirements(IMAGE_REGION_ID, {config}));
    ASSERT_TRUE(frame.FillRegionStatsData(receive, IMAGE_REGION_ID, 0));
    EXPECT_DOUBLE_EQ(data.statistics(1).value(), -12345.0);
    config.add_stats_types(CARTA::StatsType::MedAbsDevMed);
    ASSERT_TRUE(frame.SetStatsRequirements(IMAGE_REGION_ID, {config}));
    ASSERT_TRUE(frame.FillRegionStatsData(receive, IMAGE_REGION_ID, 0));
    EXPECT_DOUBLE_EQ(data.statistics(1).value(), -12345.0);
    EXPECT_TRUE(std::isfinite(data.statistics(2).value()));
    EXPECT_TRUE(cache.count(CARTA::StatsType::MedAbsDevMed));
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
    auto frame = std::make_shared<StatsCacheFrame>(0, loader, "0");

    CARTA::SetStatsRequirements_StatsConfig basic_config;
    basic_config.add_stats_types(CARTA::StatsType::Mean);
    ASSERT_TRUE(frame->SetStatsRequirements(IMAGE_REGION_ID, {basic_config}));
    CARTA::RegionStatsData stats_data;
    ASSERT_TRUE(frame->FillRegionStatsData([&stats_data](CARTA::RegionStatsData data) { stats_data = data; }, IMAGE_REGION_ID, 0));
    auto& loader_stats = loader->GetImageStats(0, 0);
    ASSERT_TRUE(loader_stats.full);
    const double cached_mean = loader_stats.basic_stats.at(CARTA::StatsType::Mean);
    ASSERT_EQ(stats_data.statistics_size(), 1);
    ASSERT_EQ(frame->_image_stats.size(), 1);
    ASSERT_DOUBLE_EQ(frame->_image_stats.begin()->second.at(CARTA::StatsType::Mean), cached_mean);

    auto stats_req_message = SetStatsRequirements(0, IMAGE_REGION_ID);
    std::vector<CARTA::SetStatsRequirements_StatsConfig> stats_configs = {
        stats_req_message.stats_configs().begin(), stats_req_message.stats_configs().end()};
    ASSERT_TRUE(frame->SetStatsRequirements(IMAGE_REGION_ID, stats_configs));

    ASSERT_TRUE(frame->FillRegionStatsData([&stats_data](CARTA::RegionStatsData data) { stats_data = data; }, IMAGE_REGION_ID, 0));
    auto mean_stat = std::find_if(stats_data.statistics().begin(), stats_data.statistics().end(),
        [](const CARTA::StatisticsValue& value) { return value.stats_type() == CARTA::StatsType::Mean; });
    ASSERT_NE(mean_stat, stats_data.statistics().end());
    EXPECT_DOUBLE_EQ(mean_stat->value(), cached_mean);
    EXPECT_DOUBLE_EQ(frame->_image_stats.begin()->second.at(CARTA::StatsType::Mean), cached_mean);

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

TEST_F(RegionStatsTest, TestHdf5FluxDensityUsesPrecomputedStatistics) {
    auto loader = carta::FileLoader::GetLoader(Hdf5Images() / "noise_10px_10px.hdf5");
    auto frame = std::make_shared<Frame>(0, loader, "0");
    auto& cached_stats = loader->GetImageStats(0, 0);
    ASSERT_TRUE(cached_stats.full);
    ASSERT_EQ(cached_stats.basic_stats.count(CARTA::StatsType::FluxDensity), 0);
    const double expected_flux = cached_stats.basic_stats.at(CARTA::StatsType::Sum);
    // A marker distinguishes the precomputed path from recalculating image pixels.
    const double cached_mean = -12345.0;
    cached_stats.basic_stats[CARTA::StatsType::Mean] = cached_mean;

    CARTA::SetStatsRequirements_StatsConfig config;
    config.add_stats_types(CARTA::StatsType::Mean);
    config.add_stats_types(CARTA::StatsType::FluxDensity);
    ASSERT_TRUE(frame->SetStatsRequirements(IMAGE_REGION_ID, {config}));

    CARTA::RegionStatsData stats_data;
    ASSERT_TRUE(frame->FillRegionStatsData([&stats_data](CARTA::RegionStatsData data) { stats_data = data; }, IMAGE_REGION_ID, 0));
    ASSERT_EQ(stats_data.statistics_size(), 2);
    EXPECT_DOUBLE_EQ(stats_data.statistics(0).value(), cached_mean);
    EXPECT_DOUBLE_EQ(stats_data.statistics(1).value(), expected_flux);
    EXPECT_DOUBLE_EQ(cached_stats.basic_stats.at(CARTA::StatsType::FluxDensity), expected_flux);
}

TEST_F(RegionStatsTest, TestFitsAnnotationRegionStats) {
    auto image_path = FitsImages() / "noise_3d.fits";
    std::vector<float> endpoints = {0.0, 0.0, 0.0, 3.0, 3.0, 3.0, 3.0, 0.0};
    CARTA::RegionStatsData stats_data;
    bool ok = RegionStats(image_path, endpoints, stats_data, true);
    ASSERT_FALSE(ok);
}
