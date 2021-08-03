/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "../CommonTestUtilities.h"
#include "BackendTester.h"
#include "DummyBackend.h"
#include "ProtobufInterface.h"

using namespace std;

class TestAnimatorDataStream : public ::testing::Test, public FileFinder, public BackendTester {
public:
    TestAnimatorDataStream() {}
    ~TestAnimatorDataStream() = default;

    void AnimatorDataStream() {
        // check the existence of sample files
        if (!FileExists(CasaImagePath("M17_SWex.image"))) {
            return;
        }

        CARTA::RegisterViewer register_viewer = GetRegisterViewer(0, "", 5);
        _dummy_backend->ReceiveMessage(register_viewer);

        // Resulting message
        std::pair<std::vector<char>, bool> message_pair;
        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            CARTA::EventType event_type = GetEventType(message);
            EXPECT_EQ(event_type, CARTA::EventType::REGISTER_VIEWER_ACK);

            if (event_type == CARTA::EventType::REGISTER_VIEWER_ACK) {
                CARTA::RegisterViewerAck register_viewer_ack = DecodeMessage<CARTA::RegisterViewerAck>(message);
                EXPECT_TRUE(register_viewer_ack.success());
            }
        }

        CARTA::CloseFile close_file = GetCloseFile(-1);
        _dummy_backend->ReceiveMessage(close_file);

        CARTA::OpenFile open_file = GetOpenFile(CasaImagePath(""), "M17_SWex.image", "0", 0, CARTA::RenderMode::RASTER);

        ElapsedTimer timer;
        timer.Start();

        _dummy_backend->ReceiveMessage(open_file);

        EXPECT_LT(timer.Elapsed(), 200);

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            CARTA::EventType event_type = GetEventType(message);

            if (event_type == CARTA::EventType::OPEN_FILE_ACK) {
                CARTA::OpenFileAck open_file_ack = DecodeMessage<CARTA::OpenFileAck>(message);
                EXPECT_TRUE(open_file_ack.success());
            }

            if (event_type == CARTA::EventType::REGION_HISTOGRAM_DATA) {
                CARTA::RegionHistogramData region_histogram_data = DecodeMessage<CARTA::RegionHistogramData>(message);
                EXPECT_EQ(region_histogram_data.file_id(), 0);
                EXPECT_EQ(region_histogram_data.stokes(), 0);
                EXPECT_EQ(region_histogram_data.region_id(), -1);
                EXPECT_EQ(region_histogram_data.progress(), 1);
                EXPECT_GE(region_histogram_data.histograms_size(), 0);
                if (region_histogram_data.histograms_size() > 0) {
                    EXPECT_EQ(region_histogram_data.histograms(0).channel(), 0);
                }
            }
        }

        CARTA::SetImageChannels set_image_channels = GetSetImageChannels(0, 0, 0, CARTA::CompressionType::ZFP, 11);

        timer.Start();

        _dummy_backend->ReceiveMessage(set_image_channels);

        EXPECT_LT(timer.Elapsed(), 200);

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            CARTA::EventType event_type = GetEventType(message);

            if (event_type == CARTA::EventType::RASTER_TILE_DATA) {
                CARTA::RasterTileData raster_tile_data = DecodeMessage<CARTA::RasterTileData>(message);
                EXPECT_EQ(raster_tile_data.file_id(), 0);
                EXPECT_EQ(raster_tile_data.channel(), 0);
                EXPECT_EQ(raster_tile_data.stokes(), 0);
            }
        }

        CARTA::SetCursor set_cursor = GetSetCursor(0, 319, 378);
        _dummy_backend->ReceiveMessage(set_cursor);

        CARTA::SetSpatialRequirements set_spatial_requirements = GetSetSpatialRequirements(0, 0);

        timer.Start();

        _dummy_backend->ReceiveMessage(set_spatial_requirements);

        EXPECT_LT(timer.Elapsed(), 100);

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            CARTA::EventType event_type = GetEventType(message);

            if (event_type == CARTA::EventType::SPATIAL_PROFILE_DATA) {
                CARTA::SpatialProfileData spatial_profile_data = DecodeMessage<CARTA::SpatialProfileData>(message);
                EXPECT_EQ(spatial_profile_data.file_id(), 0);
                EXPECT_EQ(spatial_profile_data.channel(), 0);
                EXPECT_EQ(spatial_profile_data.x(), 319);
                EXPECT_EQ(spatial_profile_data.y(), 378);
            }
        }

        CARTA::SetStatsRequirements set_stats_requirements = GetSetStatsRequirements(0, -1);

        timer.Start();

        _dummy_backend->ReceiveMessage(set_stats_requirements);

        EXPECT_LT(timer.Elapsed(), 100);

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            CARTA::EventType event_type = GetEventType(message);

            if (event_type == CARTA::EventType::REGION_STATS_DATA) {
                CARTA::RegionStatsData region_stats_data = DecodeMessage<CARTA::RegionStatsData>(message);
                EXPECT_EQ(region_stats_data.region_id(), -1);
                EXPECT_EQ(region_stats_data.channel(), 0);
            }
        }

        CARTA::SetHistogramRequirements set_histogram_requirements = GetSetHistogramRequirements(0, -1);

        timer.Start();

        _dummy_backend->ReceiveMessage(set_histogram_requirements);

        EXPECT_LT(timer.Elapsed(), 100);

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            CARTA::EventType event_type = GetEventType(message);

            if (event_type == CARTA::EventType::REGION_HISTOGRAM_DATA) {
                CARTA::RegionHistogramData region_histogram_data = DecodeMessage<CARTA::RegionHistogramData>(message);
                EXPECT_EQ(region_histogram_data.file_id(), 0);
                EXPECT_EQ(region_histogram_data.stokes(), 0);
                EXPECT_EQ(region_histogram_data.region_id(), -1);
                EXPECT_EQ(region_histogram_data.progress(), 1);
                EXPECT_GE(region_histogram_data.histograms_size(), 0);
                if (region_histogram_data.histograms_size() > 0) {
                    EXPECT_EQ(region_histogram_data.histograms(0).channel(), 0);
                }
            }
        }

        set_image_channels = GetSetImageChannels(0, 12, 0, CARTA::CompressionType::ZFP, 11);

        timer.Start();

        _dummy_backend->ReceiveMessage(set_image_channels);

        EXPECT_LT(timer.Elapsed(), 200);

        int raster_tile_data_count = 0;
        int spatial_profile_data_count = 0;
        int region_histogram_data_count = 0;
        int region_stats_data_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            CARTA::EventType event_type = GetEventType(message);

            if (event_type == CARTA::EventType::RASTER_TILE_DATA) {
                CARTA::RasterTileData raster_tile_data = DecodeMessage<CARTA::RasterTileData>(message);
                EXPECT_EQ(raster_tile_data.file_id(), 0);
                EXPECT_EQ(raster_tile_data.channel(), 12);
                EXPECT_EQ(raster_tile_data.stokes(), 0);
                ++raster_tile_data_count;
            }

            if (event_type == CARTA::EventType::SPATIAL_PROFILE_DATA) {
                CARTA::SpatialProfileData spatial_profile_data = DecodeMessage<CARTA::SpatialProfileData>(message);
                EXPECT_EQ(spatial_profile_data.file_id(), 0);
                EXPECT_EQ(spatial_profile_data.channel(), 12);
                EXPECT_EQ(spatial_profile_data.x(), 319);
                EXPECT_EQ(spatial_profile_data.y(), 378);
                ++spatial_profile_data_count;
            }

            if (event_type == CARTA::EventType::REGION_HISTOGRAM_DATA) {
                CARTA::RegionHistogramData region_histogram_data = DecodeMessage<CARTA::RegionHistogramData>(message);
                EXPECT_EQ(region_histogram_data.file_id(), 0);
                EXPECT_EQ(region_histogram_data.stokes(), 0);
                EXPECT_EQ(region_histogram_data.region_id(), -1);
                EXPECT_EQ(region_histogram_data.progress(), 1);
                EXPECT_GE(region_histogram_data.histograms_size(), 0);
                if (region_histogram_data.histograms_size() > 0) {
                    EXPECT_EQ(region_histogram_data.histograms(0).channel(), 12);
                }
                ++region_histogram_data_count;
            }

            if (event_type == CARTA::EventType::REGION_STATS_DATA) {
                CARTA::RegionStatsData region_stats_data = DecodeMessage<CARTA::RegionStatsData>(message);
                EXPECT_EQ(region_stats_data.region_id(), -1);
                EXPECT_EQ(region_stats_data.channel(), 12);
                ++region_stats_data_count;
            }
        }

        EXPECT_EQ(raster_tile_data_count, 1);
        EXPECT_EQ(spatial_profile_data_count, 1);
        EXPECT_EQ(region_histogram_data_count, 1);
        EXPECT_EQ(region_stats_data_count, 1);
    }
};

TEST_F(TestAnimatorDataStream, ANIMATOR_DATA_STREAM) {
    AnimatorDataStream();
}
