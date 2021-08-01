/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "../CommonTestUtilities.h"
#include "DummyBackend.h"
#include "ProtobufInterface.h"

using namespace std;

class TestAnimatorNavigation : public ::testing::Test, public FileFinder {
public:
    TestAnimatorNavigation() : _dummy_backend(std::make_unique<DummyBackend>()) {}

    ~TestAnimatorNavigation() = default;

    void AnimatorNavigation() {
        // check the existence of sample files
        string sample_file_name_str = Hdf5ImagePath("HH211_IQU.hdf5");
        fs::path sample_file_name(sample_file_name_str);
        if (!fs::exists(sample_file_name)) {
            spdlog::warn("File {} does not exist. Ignore the test.", sample_file_name_str);
            return;
        }

        string sample_file_name_str2 = Hdf5ImagePath("M17_SWex.hdf5");
        fs::path sample_file_name2(sample_file_name_str2);
        if (!fs::exists(sample_file_name2)) {
            spdlog::warn("File {} does not exist. Ignore the test.", sample_file_name_str2);
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
                CARTA::RegisterViewerAck register_viewer_ack = GetRegisterViewerAck(message);
                EXPECT_TRUE(register_viewer_ack.success());
            }
        }

        CARTA::CloseFile close_file = GetCloseFile(-1);

        _dummy_backend->ReceiveMessage(close_file);

        CARTA::OpenFile open_file = GetOpenFile(Hdf5ImagePath(""), "HH211_IQU.hdf5", "0", 0, CARTA::RenderMode::RASTER);

        _dummy_backend->ReceiveMessage(open_file);

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            CARTA::EventType event_type = GetEventType(message);

            if (event_type == CARTA::EventType::OPEN_FILE_ACK) {
                CARTA::OpenFileAck open_file_ack = GetOpenFileAck(message);
                EXPECT_TRUE(open_file_ack.success());
            }

            if (event_type == CARTA::EventType::REGION_HISTOGRAM_DATA) {
                CARTA::RegionHistogramData region_histogram_data = GetRegionHistogramData(message);
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

        _dummy_backend->ReceiveMessage(set_image_channels);

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            CARTA::EventType event_type = GetEventType(message);

            if (event_type == CARTA::EventType::RASTER_TILE_DATA) {
                CARTA::RasterTileData raster_tile_data = GetRasterTileData(message);
                EXPECT_EQ(raster_tile_data.file_id(), 0);
                EXPECT_EQ(raster_tile_data.channel(), 0);
                EXPECT_EQ(raster_tile_data.stokes(), 0);
            }
        }

        open_file = GetOpenFile(Hdf5ImagePath(""), "M17_SWex.hdf5", "0", 1, CARTA::RenderMode::RASTER);

        _dummy_backend->ReceiveMessage(open_file);

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            CARTA::EventType event_type = GetEventType(message);

            if (event_type == CARTA::EventType::OPEN_FILE_ACK) {
                CARTA::OpenFileAck open_file_ack = GetOpenFileAck(message);
                EXPECT_TRUE(open_file_ack.success());
            }

            if (event_type == CARTA::EventType::REGION_HISTOGRAM_DATA) {
                CARTA::RegionHistogramData region_histogram_data = GetRegionHistogramData(message);
                EXPECT_GE(region_histogram_data.histograms_size(), 0);
            }
        }

        set_image_channels = GetSetImageChannels(0, 2, 1, CARTA::CompressionType::ZFP, 11);

        _dummy_backend->ReceiveMessage(set_image_channels);

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            CARTA::EventType event_type = GetEventType(message);

            if (event_type == CARTA::EventType::RASTER_TILE_DATA) {
                CARTA::RasterTileData raster_tile_data = GetRasterTileData(message);
                EXPECT_EQ(raster_tile_data.file_id(), 0);
                EXPECT_EQ(raster_tile_data.channel(), 2);
                EXPECT_EQ(raster_tile_data.stokes(), 1);
            }
        }

        set_image_channels = GetSetImageChannels(1, 12, 0, CARTA::CompressionType::ZFP, 11);

        _dummy_backend->ReceiveMessage(set_image_channels);

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            CARTA::EventType event_type = GetEventType(message);

            if (event_type == CARTA::EventType::RASTER_TILE_DATA) {
                CARTA::RasterTileData raster_tile_data = GetRasterTileData(message);
                EXPECT_EQ(raster_tile_data.file_id(), 1);
                EXPECT_EQ(raster_tile_data.channel(), 12);
                EXPECT_EQ(raster_tile_data.stokes(), 0);
            }
        }
    }

private:
    std::unique_ptr<DummyBackend> _dummy_backend;
};

TEST_F(TestAnimatorNavigation, ANIMATOR_NAVIGATION) {
    AnimatorNavigation();
}
