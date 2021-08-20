/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "BackendTester.h"

using namespace std;

class TestRegionStatisticsRectangle : public BackendTester {
public:
    TestRegionStatisticsRectangle() {}
    ~TestRegionStatisticsRectangle() = default;

    void RegionStatisticsRectangle() {
        // Generate a FITS image
        auto filename_path_string = ImageGenerator::GeneratedFitsImagePath("640 800 25 1");
        std::filesystem::path filename_path(filename_path_string);

        int message_count = 0;

        CARTA::RegisterViewer register_viewer = GetRegisterViewer(0, "", 5);

        _dummy_backend->ReceiveMessage(register_viewer);

        // Resulting message
        std::pair<std::vector<char>, bool> message_pair;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            auto event_type = GetEventType(message);
            LogResponseEventType(event_type);
            ++message_count;
        }

        EXPECT_EQ(message_count, 1);

        CARTA::CloseFile close_file = GetCloseFile(-1);

        _dummy_backend->ReceiveMessage(close_file);

        CARTA::OpenFile open_file = GetOpenFile(filename_path.parent_path(), filename_path.filename(), "0", 0, CARTA::RenderMode::RASTER);

        _dummy_backend->ReceiveMessage(open_file);

        message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            auto event_type = GetEventType(message);
            LogResponseEventType(event_type);
            ++message_count;
        }

        EXPECT_EQ(message_count, 2);

        auto add_required_tiles = GetAddRequiredTiles(0, CARTA::CompressionType::ZFP, 11, std::vector<float>{0});

        _dummy_backend->ReceiveMessage(add_required_tiles);

        auto set_cursor = GetSetCursor(0, 1, 1);

        _dummy_backend->ReceiveMessage(set_cursor);

        message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            auto event_type = GetEventType(message);
            LogResponseEventType(event_type);
            ++message_count;
        }

        EXPECT_EQ(message_count, 4);

        auto set_region = GetSetRegion(0, -1, CARTA::RegionType::RECTANGLE, {GetPoint(212, 464), GetPoint(10, 10)}, 0);

        _dummy_backend->ReceiveMessage(set_region);

        message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            auto event_type = GetEventType(message);
            LogResponseEventType(event_type);
            if (event_type == CARTA::EventType::SET_REGION_ACK) {
                auto set_region_ack = DecodeMessage<CARTA::SetRegionAck>(message);
                EXPECT_EQ(set_region_ack.region_id(), 1);
            }
            ++message_count;
        }

        EXPECT_EQ(message_count, 1);

        auto set_stats_requirements = GetSetStatsRequirements(0, 1, "z");

        _dummy_backend->ReceiveMessage(set_stats_requirements);

        message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            auto event_type = GetEventType(message);
            LogResponseEventType(event_type);
            if (event_type == CARTA::EventType::REGION_STATS_DATA) {
                auto region_stats_data = DecodeMessage<CARTA::RegionStatsData>(message);
                EXPECT_EQ(region_stats_data.region_id(), 1);
                for (int i = 0; i < region_stats_data.statistics_size(); ++i) {
                    if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::NumPixels) {
                        EXPECT_EQ(region_stats_data.statistics(i).value(), 121);
                    }
                    if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::Sum) {
                        EXPECT_FLOAT_EQ(region_stats_data.statistics(i).value(), 15.13743);
                    }
                    if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::Mean) {
                        EXPECT_FLOAT_EQ(region_stats_data.statistics(i).value(), 0.12510273);
                    }
                    if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::RMS) {
                        EXPECT_FLOAT_EQ(region_stats_data.statistics(i).value(), 1.0475972);
                    }
                    if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::Sigma) {
                        EXPECT_FLOAT_EQ(region_stats_data.statistics(i).value(), 1.0444252);
                    }
                    if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::SumSq) {
                        EXPECT_FLOAT_EQ(region_stats_data.statistics(i).value(), 132.79263);
                    }
                    if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::Min) {
                        EXPECT_FLOAT_EQ(region_stats_data.statistics(i).value(), -2.82131);
                    }
                    if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::Max) {
                        EXPECT_FLOAT_EQ(region_stats_data.statistics(i).value(), 2.9250579);
                    }
                    if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::Extrema) {
                        EXPECT_FLOAT_EQ(region_stats_data.statistics(i).value(), 2.9250579);
                    }
                }
            }
            ++message_count;
        }

        EXPECT_EQ(message_count, 1);
    }

    void RegionStatisticsRectangleLargeImage() {
        // Check the existence of the sample image
        if (!FileExists(LargeImagePath("M17_SWex.image"))) {
            return;
        }

        int message_count = 0;

        CARTA::RegisterViewer register_viewer = GetRegisterViewer(0, "", 5);

        _dummy_backend->ReceiveMessage(register_viewer);

        // Resulting message
        std::pair<std::vector<char>, bool> message_pair;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            auto event_type = GetEventType(message);
            LogResponseEventType(event_type);
            ++message_count;
        }

        EXPECT_EQ(message_count, 1);

        CARTA::CloseFile close_file = GetCloseFile(-1);

        _dummy_backend->ReceiveMessage(close_file);

        CARTA::OpenFile open_file = GetOpenFile(LargeImagePath(""), "M17_SWex.image", "0", 0, CARTA::RenderMode::RASTER);

        _dummy_backend->ReceiveMessage(open_file);

        message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            auto event_type = GetEventType(message);
            LogResponseEventType(event_type);
            ++message_count;
        }

        EXPECT_EQ(message_count, 2);

        auto add_required_tiles = GetAddRequiredTiles(0, CARTA::CompressionType::ZFP, 11, std::vector<float>{0});

        _dummy_backend->ReceiveMessage(add_required_tiles);

        auto set_cursor = GetSetCursor(0, 1, 1);

        _dummy_backend->ReceiveMessage(set_cursor);

        message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            auto event_type = GetEventType(message);
            LogResponseEventType(event_type);
            ++message_count;
        }

        EXPECT_EQ(message_count, 4);

        auto set_region = GetSetRegion(0, -1, CARTA::RegionType::RECTANGLE, {GetPoint(212, 464), GetPoint(10, 10)}, 0);

        _dummy_backend->ReceiveMessage(set_region);

        message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            auto event_type = GetEventType(message);
            LogResponseEventType(event_type);
            if (event_type == CARTA::EventType::SET_REGION_ACK) {
                auto set_region_ack = DecodeMessage<CARTA::SetRegionAck>(message);
                EXPECT_EQ(set_region_ack.region_id(), 1);
            }
            ++message_count;
        }

        EXPECT_EQ(message_count, 1);

        auto set_stats_requirements = GetSetStatsRequirements(0, 1, "z");

        _dummy_backend->ReceiveMessage(set_stats_requirements);

        message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            auto event_type = GetEventType(message);
            LogResponseEventType(event_type);
            if (event_type == CARTA::EventType::REGION_STATS_DATA) {
                auto region_stats_data = DecodeMessage<CARTA::RegionStatsData>(message);
                EXPECT_EQ(region_stats_data.region_id(), 1);
                for (int i = 0; i < region_stats_data.statistics_size(); ++i) {
                    if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::NumPixels) {
                        EXPECT_EQ(region_stats_data.statistics(i).value(), 121);
                    }
                    if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::Sum) {
                        EXPECT_FLOAT_EQ(region_stats_data.statistics(i).value(), 0.28389804);
                    }
                    if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::FluxDensity) {
                        EXPECT_FLOAT_EQ(region_stats_data.statistics(i).value(), 0.01304297);
                    }
                    if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::Mean) {
                        EXPECT_FLOAT_EQ(region_stats_data.statistics(i).value(), 0.0023462647);
                    }
                    if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::RMS) {
                        EXPECT_FLOAT_EQ(region_stats_data.statistics(i).value(), 0.0038839388);
                    }
                    if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::Sigma) {
                        EXPECT_FLOAT_EQ(region_stats_data.statistics(i).value(), 0.00310803);
                    }
                    if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::SumSq) {
                        EXPECT_FLOAT_EQ(region_stats_data.statistics(i).value(), 0.0018252826);
                    }
                    if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::Min) {
                        EXPECT_FLOAT_EQ(region_stats_data.statistics(i).value(), -0.0035811267);
                    }
                    if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::Max) {
                        EXPECT_FLOAT_EQ(region_stats_data.statistics(i).value(), 0.00793927);
                    }
                    if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::Extrema) {
                        EXPECT_FLOAT_EQ(region_stats_data.statistics(i).value(), 0.007939267);
                    }
                }
            }
            ++message_count;
        }

        EXPECT_EQ(message_count, 1);
    }
};

TEST_F(TestRegionStatisticsRectangle, REGION_STATISTICS_RECTANGLE) {
    RegionStatisticsRectangle();
}

TEST_F(TestRegionStatisticsRectangle, REGION_STATISTICS_RECTANGLE_LARGE_IMAGE) {
    RegionStatisticsRectangleLargeImage();
}
