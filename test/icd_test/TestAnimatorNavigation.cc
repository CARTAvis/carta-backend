/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "BackendTester.h"

using namespace std;

class TestAnimatorNavigation : public BackendTester {
public:
    TestAnimatorNavigation() {}
    ~TestAnimatorNavigation() = default;

    void AnimatorNavigation() {
        // Generate two HDF5 images
        auto first_filename_path_string = ImageGenerator::GeneratedHdf5ImagePath("1049 1049 5 3");
        std::filesystem::path first_filename_path(first_filename_path_string);

        auto second_filename_path_string = ImageGenerator::GeneratedHdf5ImagePath("640 800 25 1");
        std::filesystem::path second_filename_path(second_filename_path_string);

        int message_count = 0;

        CARTA::RegisterViewer register_viewer = GetRegisterViewer(0, "", 5);

        _dummy_backend->ReceiveMessage(register_viewer);

        // Resulting message
        std::pair<std::vector<char>, bool> message_pair;
        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            CARTA::EventType event_type = GetEventType(message);
            LogResponseEventType(event_type);

            if (event_type == CARTA::EventType::REGISTER_VIEWER_ACK) {
                CARTA::RegisterViewerAck register_viewer_ack = DecodeMessage<CARTA::RegisterViewerAck>(message);
                EXPECT_TRUE(register_viewer_ack.success());
            }
            ++message_count;
        }

        EXPECT_EQ(message_count, 1);

        CARTA::CloseFile close_file = GetCloseFile(-1);

        _dummy_backend->ReceiveMessage(close_file);

        CARTA::OpenFile open_file =
            GetOpenFile(first_filename_path.parent_path(), first_filename_path.filename(), "0", 0, CARTA::RenderMode::RASTER);

        ElapsedTimer timer;
        timer.Start();

        _dummy_backend->ReceiveMessage(open_file);

        EXPECT_LT(timer.Elapsed(), 200);

        message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            CARTA::EventType event_type = GetEventType(message);
            LogResponseEventType(event_type);

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

        _dummy_backend->ReceiveMessage(set_image_channels);

        EXPECT_LT(timer.Elapsed(), 200);

        message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            CARTA::EventType event_type = GetEventType(message);
            LogResponseEventType(event_type);

            if (event_type == CARTA::EventType::RASTER_TILE_DATA) {
                CARTA::RasterTileData raster_tile_data = DecodeMessage<CARTA::RasterTileData>(message);
                EXPECT_EQ(raster_tile_data.file_id(), 0);
                EXPECT_EQ(raster_tile_data.channel(), 0);
                EXPECT_EQ(raster_tile_data.stokes(), 0);
            }
            ++message_count;
        }

        EXPECT_EQ(message_count, 3);

        open_file = GetOpenFile(second_filename_path.parent_path(), second_filename_path.filename(), "0", 1, CARTA::RenderMode::RASTER);

        _dummy_backend->ReceiveMessage(open_file);

        message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            CARTA::EventType event_type = GetEventType(message);
            LogResponseEventType(event_type);

            if (event_type == CARTA::EventType::OPEN_FILE_ACK) {
                CARTA::OpenFileAck open_file_ack = DecodeMessage<CARTA::OpenFileAck>(message);
                EXPECT_TRUE(open_file_ack.success());
            }

            if (event_type == CARTA::EventType::REGION_HISTOGRAM_DATA) {
                CARTA::RegionHistogramData region_histogram_data = DecodeMessage<CARTA::RegionHistogramData>(message);
                EXPECT_EQ(region_histogram_data.file_id(), 1);
                EXPECT_EQ(region_histogram_data.region_id(), -1);
                EXPECT_EQ(region_histogram_data.channel(), 0);
                EXPECT_EQ(region_histogram_data.stokes(), 0);
                EXPECT_EQ(region_histogram_data.progress(), 1);
                EXPECT_TRUE(region_histogram_data.has_histograms());
            }
            ++message_count;
        }

        EXPECT_EQ(message_count, 2);

        set_image_channels = GetSetImageChannels(0, 2, 1, CARTA::CompressionType::ZFP, 11);

        timer.Start();

        _dummy_backend->ReceiveMessage(set_image_channels);

        EXPECT_LT(timer.Elapsed(), 200);

        message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            CARTA::EventType event_type = GetEventType(message);
            LogResponseEventType(event_type);

            if (event_type == CARTA::EventType::RASTER_TILE_DATA) {
                CARTA::RasterTileData raster_tile_data = DecodeMessage<CARTA::RasterTileData>(message);
                EXPECT_EQ(raster_tile_data.file_id(), 0);
                EXPECT_EQ(raster_tile_data.channel(), 2);
                EXPECT_EQ(raster_tile_data.stokes(), 1);
            }
            ++message_count;
        }

        EXPECT_EQ(message_count, 4);

        set_image_channels = GetSetImageChannels(1, 12, 0, CARTA::CompressionType::ZFP, 11);

        timer.Start();

        _dummy_backend->ReceiveMessage(set_image_channels);

        EXPECT_LT(timer.Elapsed(), 200);

        message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            CARTA::EventType event_type = GetEventType(message);
            LogResponseEventType(event_type);

            if (event_type == CARTA::EventType::RASTER_TILE_DATA) {
                CARTA::RasterTileData raster_tile_data = DecodeMessage<CARTA::RasterTileData>(message);
                EXPECT_EQ(raster_tile_data.file_id(), 1);
                EXPECT_EQ(raster_tile_data.channel(), 12);
                EXPECT_EQ(raster_tile_data.stokes(), 0);
            }
            ++message_count;
        }

        EXPECT_EQ(message_count, 4);
    }
};

TEST_F(TestAnimatorNavigation, ANIMATOR_NAVIGATION) {
    AnimatorNavigation();
}
