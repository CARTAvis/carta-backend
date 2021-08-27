/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "BackendTester.h"

class TestRegionStatisticsEllipse : public BackendTester {
public:
    TestRegionStatisticsEllipse() {}
    ~TestRegionStatisticsEllipse() = default;

    void RegionStatisticsEllipse() {
        // Generate a FITS image
        auto filename_path_string = ImageGenerator::GeneratedFitsImagePath("640 800 25 1");
        std::filesystem::path filename_path(filename_path_string);

        std::atomic<int> message_count = 0;

        CARTA::RegisterViewer register_viewer = GetRegisterViewer(0, "", 5);

        _dummy_backend->Receive(register_viewer);

        // Resulting message
        std::pair<std::vector<char>, bool> message_pair;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            auto event_type = GetEventType(message);
            LogResponsiveEventType(event_type);
            ++message_count;
        }

        EXPECT_EQ(message_count, 1);

        CARTA::CloseFile close_file = GetCloseFile(-1);

        _dummy_backend->Receive(close_file);

        CARTA::OpenFile open_file = GetOpenFile(filename_path.parent_path(), filename_path.filename(), "0", 0, CARTA::RenderMode::RASTER);

        _dummy_backend->Receive(open_file);

        message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            auto event_type = GetEventType(message);
            LogResponsiveEventType(event_type);
            ++message_count;
        }

        EXPECT_EQ(message_count, 2);

        auto add_required_tiles = GetAddRequiredTiles(0, CARTA::CompressionType::ZFP, 11, std::vector<float>{0});

        _dummy_backend->Receive(add_required_tiles);
        _dummy_backend->WaitForJobFinished();

        auto set_cursor = GetSetCursor(0, 1, 1);

        _dummy_backend->Receive(set_cursor);
        _dummy_backend->WaitForJobFinished();

        message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            auto event_type = GetEventType(message);
            LogResponsiveEventType(event_type);
            ++message_count;
        }

        EXPECT_EQ(message_count, 4);

        auto set_region = GetSetRegion(0, -1, CARTA::RegionType::ELLIPSE, {GetPoint(114, 545), GetPoint(4, 2)}, 0);

        _dummy_backend->Receive(set_region);

        message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            auto event_type = GetEventType(message);
            LogResponsiveEventType(event_type);
            if (event_type == CARTA::EventType::SET_REGION_ACK) {
                auto set_region_ack = DecodeMessage<CARTA::SetRegionAck>(message);
                EXPECT_EQ(set_region_ack.region_id(), 1);
            }
            ++message_count;
        }

        EXPECT_EQ(message_count, 1);

        auto set_stats_requirements = GetSetStatsRequirements(0, 1, "z");

        _dummy_backend->Receive(set_stats_requirements);
        _dummy_backend->WaitForJobFinished();

        message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            auto event_type = GetEventType(message);
            LogResponsiveEventType(event_type);
            if (event_type == CARTA::EventType::REGION_STATS_DATA) {
                auto region_stats_data = DecodeMessage<CARTA::RegionStatsData>(message);
                EXPECT_EQ(region_stats_data.region_id(), 1);
                for (int i = 0; i < region_stats_data.statistics_size(); ++i) {
                    if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::NumPixels) {
                        EXPECT_EQ(region_stats_data.statistics(i).value(), 24);
                    } else if (region_stats_data.statistics(i).stats_type() == CARTA::StatsType::FluxDensity) {
                        EXPECT_TRUE(std::isnan(region_stats_data.statistics(i).value()));
                    } else {
                        EXPECT_TRUE(!std::isnan(region_stats_data.statistics(i).value()));
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
