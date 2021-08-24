/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "BackendTester.h"

using namespace std;

class TestAnimatorDataStream : public BackendTester {
public:
    TestAnimatorDataStream() {}
    ~TestAnimatorDataStream() = default;

    void AnimatorDataStream() {
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
            CARTA::EventType event_type = GetEventType(message);
            LogResponsiveEventType(event_type);

            if (event_type == CARTA::EventType::REGISTER_VIEWER_ACK) {
                CARTA::RegisterViewerAck register_viewer_ack = DecodeMessage<CARTA::RegisterViewerAck>(message);
                EXPECT_TRUE(register_viewer_ack.success());
            }
            ++message_count;
        }

        EXPECT_EQ(message_count, 1);

        CARTA::CloseFile close_file = GetCloseFile(-1);

        _dummy_backend->Receive(close_file);

        CARTA::OpenFile open_file = GetOpenFile(filename_path.parent_path(), filename_path.filename(), "0", 0, CARTA::RenderMode::RASTER);

        ElapsedTimer timer;
        timer.Start();

        _dummy_backend->Receive(open_file);

        EXPECT_LT(timer.Elapsed(), 200);

        message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            CARTA::EventType event_type = GetEventType(message);
            LogResponsiveEventType(event_type);

            if (event_type == CARTA::EventType::OPEN_FILE_ACK) {
                CARTA::OpenFileAck open_file_ack = DecodeMessage<CARTA::OpenFileAck>(message);
                EXPECT_TRUE(open_file_ack.success());
            }

            if (event_type == CARTA::EventType::REGION_HISTOGRAM_DATA) {
                CARTA::RegionHistogramData region_histogram_data = DecodeMessage<CARTA::RegionHistogramData>(message);
                EXPECT_EQ(region_histogram_data.file_id(), 0);
                EXPECT_EQ(region_histogram_data.region_id(), -1);
                EXPECT_EQ(region_histogram_data.channel(), 0);
                EXPECT_EQ(region_histogram_data.stokes(), 0);
                EXPECT_EQ(region_histogram_data.progress(), 1);
                EXPECT_TRUE(region_histogram_data.has_histograms());
            }
            ++message_count;
        }

        EXPECT_EQ(message_count, 2);

        CARTA::SetImageChannels set_image_channels = GetSetImageChannels(0, 0, 0, CARTA::CompressionType::ZFP, 11);

        timer.Start();

        _dummy_backend->Receive(set_image_channels);

        _dummy_backend->WaitForJobFinished();

        EXPECT_LT(timer.Elapsed(), 200);

        message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            CARTA::EventType event_type = GetEventType(message);
            LogResponsiveEventType(event_type);

            if (event_type == CARTA::EventType::RASTER_TILE_DATA) {
                CARTA::RasterTileData raster_tile_data = DecodeMessage<CARTA::RasterTileData>(message);
                EXPECT_EQ(raster_tile_data.file_id(), 0);
                EXPECT_EQ(raster_tile_data.channel(), 0);
                EXPECT_EQ(raster_tile_data.stokes(), 0);
            }
            ++message_count;
        }

        EXPECT_EQ(message_count, 3);

        CARTA::SetCursor set_cursor = GetSetCursor(0, 319, 378);

        _dummy_backend->Receive(set_cursor);
        _dummy_backend->WaitForJobFinished();

        CARTA::SetSpatialRequirements set_spatial_requirements = GetSetSpatialRequirements(0, 0);

        timer.Start();

        _dummy_backend->Receive(set_spatial_requirements);
        _dummy_backend->WaitForJobFinished();

        EXPECT_LT(timer.Elapsed(), 100);

        message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            CARTA::EventType event_type = GetEventType(message);
            LogResponsiveEventType(event_type);

            if (event_type == CARTA::EventType::SPATIAL_PROFILE_DATA) {
                CARTA::SpatialProfileData spatial_profile_data = DecodeMessage<CARTA::SpatialProfileData>(message);
                EXPECT_EQ(spatial_profile_data.file_id(), 0);
                EXPECT_EQ(spatial_profile_data.channel(), 0);
                EXPECT_EQ(spatial_profile_data.x(), 319);
                EXPECT_EQ(spatial_profile_data.y(), 378);
            }
            ++message_count;
        }

        EXPECT_EQ(message_count, 2);

        CARTA::SetStatsRequirements set_stats_requirements = GetSetStatsRequirements(0, -1);

        timer.Start();

        _dummy_backend->Receive(set_stats_requirements);

        _dummy_backend->WaitForJobFinished();

        EXPECT_LT(timer.Elapsed(), 100);

        message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            CARTA::EventType event_type = GetEventType(message);
            LogResponsiveEventType(event_type);

            if (event_type == CARTA::EventType::REGION_STATS_DATA) {
                CARTA::RegionStatsData region_stats_data = DecodeMessage<CARTA::RegionStatsData>(message);
                EXPECT_EQ(region_stats_data.region_id(), -1);
                EXPECT_EQ(region_stats_data.channel(), 0);
            }
            ++message_count;
        }

        EXPECT_EQ(message_count, 1);

        CARTA::SetHistogramRequirements set_histogram_requirements = GetSetHistogramRequirements(0, -1);

        timer.Start();

        _dummy_backend->Receive(set_histogram_requirements);

        _dummy_backend->WaitForJobFinished();

        EXPECT_LT(timer.Elapsed(), 100);

        message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            CARTA::EventType event_type = GetEventType(message);
            LogResponsiveEventType(event_type);

            if (event_type == CARTA::EventType::REGION_HISTOGRAM_DATA) {
                CARTA::RegionHistogramData region_histogram_data = DecodeMessage<CARTA::RegionHistogramData>(message);
                EXPECT_EQ(region_histogram_data.file_id(), 0);
                EXPECT_EQ(region_histogram_data.region_id(), -1);
                EXPECT_EQ(region_histogram_data.channel(), 0);
                EXPECT_EQ(region_histogram_data.stokes(), 0);
                EXPECT_EQ(region_histogram_data.progress(), 1);
                EXPECT_TRUE(region_histogram_data.has_histograms());
            }
            ++message_count;
        }

        EXPECT_EQ(message_count, 1);

        set_image_channels = GetSetImageChannels(0, 12, 0, CARTA::CompressionType::ZFP, 11);

        timer.Start();

        _dummy_backend->Receive(set_image_channels);

        _dummy_backend->WaitForJobFinished();

        EXPECT_LT(timer.Elapsed(), 200);

        message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            CARTA::EventType event_type = GetEventType(message);
            LogResponsiveEventType(event_type);

            if (event_type == CARTA::EventType::RASTER_TILE_DATA) {
                CARTA::RasterTileData raster_tile_data = DecodeMessage<CARTA::RasterTileData>(message);
                EXPECT_EQ(raster_tile_data.file_id(), 0);
                EXPECT_EQ(raster_tile_data.channel(), 12);
                EXPECT_EQ(raster_tile_data.stokes(), 0);
            }

            if (event_type == CARTA::EventType::SPATIAL_PROFILE_DATA) {
                CARTA::SpatialProfileData spatial_profile_data = DecodeMessage<CARTA::SpatialProfileData>(message);
                EXPECT_EQ(spatial_profile_data.file_id(), 0);
                EXPECT_EQ(spatial_profile_data.channel(), 12);
                EXPECT_EQ(spatial_profile_data.x(), 319);
                EXPECT_EQ(spatial_profile_data.y(), 378);
            }

            if (event_type == CARTA::EventType::REGION_HISTOGRAM_DATA) {
                CARTA::RegionHistogramData region_histogram_data = DecodeMessage<CARTA::RegionHistogramData>(message);
                EXPECT_EQ(region_histogram_data.file_id(), 0);
                EXPECT_EQ(region_histogram_data.region_id(), -1);
                EXPECT_EQ(region_histogram_data.channel(), 12);
                EXPECT_EQ(region_histogram_data.stokes(), 0);
                EXPECT_EQ(region_histogram_data.progress(), 1);
                EXPECT_TRUE(region_histogram_data.has_histograms());
            }

            if (event_type == CARTA::EventType::REGION_STATS_DATA) {
                CARTA::RegionStatsData region_stats_data = DecodeMessage<CARTA::RegionStatsData>(message);
                EXPECT_EQ(region_stats_data.region_id(), -1);
                EXPECT_EQ(region_stats_data.channel(), 12);
            }
            ++message_count;
        }

        EXPECT_EQ(message_count, 6);
    }
};

TEST_F(TestAnimatorDataStream, ANIMATOR_DATA_STREAM) {
    AnimatorDataStream();
}
