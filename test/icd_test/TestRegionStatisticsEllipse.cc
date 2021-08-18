/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "BackendTester.h"

using namespace std;

class TestRegionStatisticsEllipse : public BackendTester {
public:
    TestRegionStatisticsEllipse() {}
    ~TestRegionStatisticsEllipse() = default;

    void RegionStatisticsEllipse() {
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

        auto add_required_tiles = GetAddRequiredTiles(0, CARTA::CompressionType::ZFP, 11);

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

        auto set_region = GetSetRegion(0, -1, CARTA::RegionType::ELLIPSE, {GetPoint(114, 545), GetPoint(4, 2)}, 0);

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
                        EXPECT_EQ(region_stats_data.statistics(i).value(), 24);
                    }
                    if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::Sum) {
                        EXPECT_FLOAT_EQ(region_stats_data.statistics(i).value(), 9.4404621);
                    }
                    if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::Mean) {
                        EXPECT_FLOAT_EQ(region_stats_data.statistics(i).value(), 0.3933526);
                    }
                    if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::RMS) {
                        EXPECT_FLOAT_EQ(region_stats_data.statistics(i).value(), 0.9301033);
                    }
                    if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::Sigma) {
                        EXPECT_FLOAT_EQ(region_stats_data.statistics(i).value(), 0.86095959);
                    }
                    if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::SumSq) {
                        EXPECT_FLOAT_EQ(region_stats_data.statistics(i).value(), 20.762211);
                    }
                    if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::Min) {
                        EXPECT_FLOAT_EQ(region_stats_data.statistics(i).value(), -1.3681358);
                    }
                    if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::Max) {
                        EXPECT_FLOAT_EQ(region_stats_data.statistics(i).value(), 2.129252);
                    }
                    if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::Extrema) {
                        EXPECT_FLOAT_EQ(region_stats_data.statistics(i).value(), 2.129252);
                    }
                }
            }
            ++message_count;
        }

        EXPECT_EQ(message_count, 1);
    }

    void RegionStatisticsEllipseLargeImage() {
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

        auto add_required_tiles = GetAddRequiredTiles(0, CARTA::CompressionType::ZFP, 11);

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

        auto set_region = GetSetRegion(0, -1, CARTA::RegionType::ELLIPSE, {GetPoint(114, 545), GetPoint(4, 2)}, 0);

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
                        EXPECT_EQ(region_stats_data.statistics(i).value(), 24);
                    }
                    if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::Sum) {
                        EXPECT_FLOAT_EQ(region_stats_data.statistics(i).value(), 0.18536625);
                    }
                    if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::FluxDensity) {
                        EXPECT_FLOAT_EQ(region_stats_data.statistics(i).value(), 0.00851618);
                    }
                    if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::Mean) {
                        EXPECT_FLOAT_EQ(region_stats_data.statistics(i).value(), 0.0077235936);
                    }
                    if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::RMS) {
                        EXPECT_FLOAT_EQ(region_stats_data.statistics(i).value(), 0.013971736);
                    }
                    if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::Sigma) {
                        EXPECT_FLOAT_EQ(region_stats_data.statistics(i).value(), 0.01189324);
                    }
                    if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::SumSq) {
                        EXPECT_FLOAT_EQ(region_stats_data.statistics(i).value(), 0.0046850257);
                    }
                    if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::Min) {
                        EXPECT_FLOAT_EQ(region_stats_data.statistics(i).value(), -0.01768329);
                    }
                    if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::Max) {
                        EXPECT_FLOAT_EQ(region_stats_data.statistics(i).value(), 0.02505673);
                    }
                    if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::Extrema) {
                        EXPECT_FLOAT_EQ(region_stats_data.statistics(i).value(), 0.025056729);
                    }
                }
            }
            ++message_count;
        }

        EXPECT_EQ(message_count, 1);
    }
};

TEST_F(TestRegionStatisticsEllipse, REGION_STATISTICS_ELLIPSE) {
    RegionStatisticsEllipse();
}

TEST_F(TestRegionStatisticsEllipse, REGION_STATISTICS_ELLIPSE_LARGE_IMAGE) {
    RegionStatisticsEllipseLargeImage();
}
