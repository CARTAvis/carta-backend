/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>
#include <memory>

#include "BackendModel.h"
#include "CommonTestUtilities.h"
#include "Timer/Timer.h"
#include "Util/Message.h"

class IcdTest : public ::testing::Test, public FileFinder {
    std::unique_ptr<BackendModel> _dummy_backend;
    std::pair<std::vector<char>, bool> _message_pair; // Resulting message
    int _message_count = 0;

public:
    IcdTest() {
        _dummy_backend = BackendModel::GetDummyBackend();
    }
    ~IcdTest() = default;

    void AccessCarta(uint32_t session_id, string api_key, uint32_t client_feature_flags, CARTA::SessionType expected_session_type,
        bool expected_message) {
        CARTA::RegisterViewer register_viewer = Message::RegisterViewer(session_id, api_key, client_feature_flags);

        carta::Timer t;

        _dummy_backend->Receive(register_viewer);

        EXPECT_LT(t.Elapsed().ms(), 100); // expect the process time within 100 ms

        _message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(_message_pair)) {
            std::vector<char> message = _message_pair.first;
            CARTA::EventType event_type = Message::EventType(message);

            if (event_type == CARTA::EventType::REGISTER_VIEWER_ACK) {
                CARTA::RegisterViewerAck register_viewer_ack = Message::DecodeMessage<CARTA::RegisterViewerAck>(message);
                EXPECT_TRUE(register_viewer_ack.success());
                EXPECT_EQ(register_viewer_ack.session_id(), session_id);
                EXPECT_EQ(register_viewer_ack.session_type(), expected_session_type);
                EXPECT_EQ(register_viewer_ack.user_preferences_size(), 0);
                EXPECT_EQ(register_viewer_ack.user_layouts_size(), 0);
                if (expected_message) {
                    EXPECT_GT(register_viewer_ack.message().length(), 0);
                } else {
                    EXPECT_EQ(register_viewer_ack.message().length(), 0);
                }
            }
            ++_message_count;
        }

        EXPECT_EQ(_message_count, 1);
    }

    void AnimatorDataStream() {
        // Generate a FITS image
        auto filename_path_string = ImageGenerator::GeneratedFitsImagePath("640 800 25 1");
        std::filesystem::path filename_path(filename_path_string);

        CARTA::OpenFile open_file =
            Message::OpenFile(filename_path.parent_path(), filename_path.filename(), "0", 0, CARTA::RenderMode::RASTER);

        _dummy_backend->Receive(open_file);

        _message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(_message_pair)) {
            std::vector<char> message = _message_pair.first;
            CARTA::EventType event_type = Message::EventType(message);

            if (event_type == CARTA::EventType::OPEN_FILE_ACK) {
                CARTA::OpenFileAck open_file_ack = Message::DecodeMessage<CARTA::OpenFileAck>(message);
                EXPECT_TRUE(open_file_ack.success());
            }

            if (event_type == CARTA::EventType::REGION_HISTOGRAM_DATA) {
                CARTA::RegionHistogramData region_histogram_data = Message::DecodeMessage<CARTA::RegionHistogramData>(message);
                EXPECT_EQ(region_histogram_data.file_id(), 0);
                EXPECT_EQ(region_histogram_data.region_id(), -1);
                EXPECT_EQ(region_histogram_data.channel(), 0);
                EXPECT_EQ(region_histogram_data.stokes(), 0);
                EXPECT_EQ(region_histogram_data.progress(), 1);
                EXPECT_TRUE(region_histogram_data.has_histograms());
            }
            ++_message_count;
        }

        EXPECT_EQ(_message_count, 2); // OPEN_FILE_ACK x1 + REGION_HISTOGRAM_DATA x1

        CARTA::SetImageChannels set_image_channels = Message::SetImageChannels(0, 0, 0, CARTA::CompressionType::ZFP, 11);

        _dummy_backend->Receive(set_image_channels);

        _dummy_backend->WaitForJobFinished();

        _message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(_message_pair)) {
            std::vector<char> message = _message_pair.first;
            CARTA::EventType event_type = Message::EventType(message);

            if (event_type == CARTA::EventType::RASTER_TILE_DATA) {
                CARTA::RasterTileData raster_tile_data = Message::DecodeMessage<CARTA::RasterTileData>(message);
                EXPECT_EQ(raster_tile_data.file_id(), 0);
                EXPECT_EQ(raster_tile_data.channel(), 0);
                EXPECT_EQ(raster_tile_data.stokes(), 0);
            }
            ++_message_count;
        }

        EXPECT_EQ(_message_count, 3); // RASTER_TILE_DATA x3

        set_image_channels = Message::SetImageChannels(0, 12, 0, CARTA::CompressionType::ZFP, 11);

        _dummy_backend->Receive(set_image_channels);

        _dummy_backend->WaitForJobFinished();

        _message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(_message_pair)) {
            std::vector<char> message = _message_pair.first;
            CARTA::EventType event_type = Message::EventType(message);

            if (event_type == CARTA::EventType::RASTER_TILE_DATA) {
                CARTA::RasterTileData raster_tile_data = Message::DecodeMessage<CARTA::RasterTileData>(message);
                EXPECT_EQ(raster_tile_data.file_id(), 0);
                EXPECT_EQ(raster_tile_data.channel(), 12);
                EXPECT_EQ(raster_tile_data.stokes(), 0);
            }

            if (event_type == CARTA::EventType::REGION_HISTOGRAM_DATA) {
                CARTA::RegionHistogramData region_histogram_data = Message::DecodeMessage<CARTA::RegionHistogramData>(message);
                EXPECT_EQ(region_histogram_data.file_id(), 0);
                EXPECT_EQ(region_histogram_data.region_id(), -1);
                EXPECT_EQ(region_histogram_data.channel(), 12);
                EXPECT_EQ(region_histogram_data.stokes(), 0);
                EXPECT_EQ(region_histogram_data.progress(), 1);
                EXPECT_TRUE(region_histogram_data.has_histograms());
            }
            ++_message_count;
        }

        EXPECT_EQ(_message_count, 4); // RASTER_TILE_DATA x3 + REGION_HISTOGRAM_DATA x1
    }

    void AnimatorNavigation() {
        // Generate two HDF5 images
        auto first_filename_path_string = ImageGenerator::GeneratedHdf5ImagePath("1049 1049 5 3");
        std::filesystem::path first_filename_path(first_filename_path_string);

        auto second_filename_path_string = ImageGenerator::GeneratedHdf5ImagePath("640 800 25 1");
        std::filesystem::path second_filename_path(second_filename_path_string);

        CARTA::OpenFile open_file =
            Message::OpenFile(first_filename_path.parent_path(), first_filename_path.filename(), "0", 0, CARTA::RenderMode::RASTER);

        _dummy_backend->Receive(open_file);

        _dummy_backend->ClearMessagesQueue();

        CARTA::SetImageChannels set_image_channels = Message::SetImageChannels(0, 0, 0, CARTA::CompressionType::ZFP, 11);

        _dummy_backend->Receive(set_image_channels);

        _dummy_backend->WaitForJobFinished();

        _message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(_message_pair)) {
            std::vector<char> message = _message_pair.first;
            CARTA::EventType event_type = Message::EventType(message);
            if (event_type == CARTA::EventType::RASTER_TILE_DATA) {
                CARTA::RasterTileData raster_tile_data = Message::DecodeMessage<CARTA::RasterTileData>(message);
                EXPECT_EQ(raster_tile_data.file_id(), 0);
                EXPECT_EQ(raster_tile_data.channel(), 0);
                EXPECT_EQ(raster_tile_data.stokes(), 0);
            }
            ++_message_count;
        }

        EXPECT_EQ(_message_count, 3);

        open_file =
            Message::OpenFile(second_filename_path.parent_path(), second_filename_path.filename(), "0", 1, CARTA::RenderMode::RASTER);

        _dummy_backend->Receive(open_file);

        _message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(_message_pair)) {
            std::vector<char> message = _message_pair.first;
            CARTA::EventType event_type = Message::EventType(message);
            if (event_type == CARTA::EventType::OPEN_FILE_ACK) {
                CARTA::OpenFileAck open_file_ack = Message::DecodeMessage<CARTA::OpenFileAck>(message);
                EXPECT_TRUE(open_file_ack.success());
            }

            if (event_type == CARTA::EventType::REGION_HISTOGRAM_DATA) {
                CARTA::RegionHistogramData region_histogram_data = Message::DecodeMessage<CARTA::RegionHistogramData>(message);
                EXPECT_EQ(region_histogram_data.file_id(), 1);
                EXPECT_EQ(region_histogram_data.region_id(), -1);
                EXPECT_EQ(region_histogram_data.channel(), 0);
                EXPECT_EQ(region_histogram_data.stokes(), 0);
                EXPECT_EQ(region_histogram_data.progress(), 1);
                EXPECT_TRUE(region_histogram_data.has_histograms());
            }
            ++_message_count;
        }

        EXPECT_EQ(_message_count, 2);

        set_image_channels = Message::SetImageChannels(0, 2, 1, CARTA::CompressionType::ZFP, 11);

        _dummy_backend->Receive(set_image_channels);

        _dummy_backend->WaitForJobFinished();

        _message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(_message_pair)) {
            std::vector<char> message = _message_pair.first;
            CARTA::EventType event_type = Message::EventType(message);
            if (event_type == CARTA::EventType::RASTER_TILE_DATA) {
                CARTA::RasterTileData raster_tile_data = Message::DecodeMessage<CARTA::RasterTileData>(message);
                EXPECT_EQ(raster_tile_data.file_id(), 0);
                EXPECT_EQ(raster_tile_data.channel(), 2);
                EXPECT_EQ(raster_tile_data.stokes(), 1);
            }
            ++_message_count;
        }

        EXPECT_EQ(_message_count, 4);

        set_image_channels = Message::SetImageChannels(1, 12, 0, CARTA::CompressionType::ZFP, 11);

        _dummy_backend->Receive(set_image_channels);

        _dummy_backend->WaitForJobFinished();

        _message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(_message_pair)) {
            std::vector<char> message = _message_pair.first;
            CARTA::EventType event_type = Message::EventType(message);
            if (event_type == CARTA::EventType::RASTER_TILE_DATA) {
                CARTA::RasterTileData raster_tile_data = Message::DecodeMessage<CARTA::RasterTileData>(message);
                EXPECT_EQ(raster_tile_data.file_id(), 1);
                EXPECT_EQ(raster_tile_data.channel(), 12);
                EXPECT_EQ(raster_tile_data.stokes(), 0);
            }
            ++_message_count;
        }

        EXPECT_EQ(_message_count, 4);
    }

    void AnimatorPlayback() {
        auto filename_path_string = ImageGenerator::GeneratedFitsImagePath("640 800 25 1");
        std::filesystem::path filename_path(filename_path_string);

        CARTA::OpenFile open_file =
            Message::OpenFile(filename_path.parent_path(), filename_path.filename(), "0", 0, CARTA::RenderMode::RASTER);

        _dummy_backend->Receive(open_file);

        _message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(_message_pair)) {
            std::vector<char> message = _message_pair.first;
            auto event_type = Message::EventType(message);
            ++_message_count;
        }

        EXPECT_EQ(_message_count, 2);

        std::vector<float> tiles = {33558529.0, 33558528.0, 33562625.0, 33554433.0, 33562624.0, 33558530.0, 33554432.0, 33562626.0,
            33554434.0, 33566721.0, 33566720.0, 33566722.0};

        auto add_required_tiles = Message::AddRequiredTiles(0, CARTA::CompressionType::ZFP, 11, tiles);

        _dummy_backend->Receive(add_required_tiles);

        _dummy_backend->WaitForJobFinished();

        _message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(_message_pair)) {
            std::vector<char> message = _message_pair.first;
            auto event_type = Message::EventType(message);
            ++_message_count;
        }

        EXPECT_EQ(_message_count, 14);

        // Play animation forward

        int first_channel(0);
        int start_channel(1);
        int end_channel(10);
        int last_channel(24);
        int delta_channel(1);
        int frame_rate(5);
        int stokes(0);

        std::pair<int32_t, int32_t> first_frame = std::make_pair(first_channel, stokes);
        std::pair<int32_t, int32_t> start_frame = std::make_pair(start_channel, stokes);
        std::pair<int32_t, int32_t> end_frame = std::make_pair(end_channel, stokes);
        std::pair<int32_t, int32_t> last_frame = std::make_pair(last_channel, stokes);
        std::pair<int32_t, int32_t> delta_frame = std::make_pair(delta_channel, stokes);

        tiles = {33554432.0, 33558528.0, 33562624.0, 33566720.0, 33554433.0, 33558529.0, 33562625.0, 33566721.0, 33554434.0, 33558530.0,
            33562626.0, 33566722.0};

        auto start_animation = Message::StartAnimation(
            0, first_frame, start_frame, last_frame, delta_frame, CARTA::CompressionType::ZFP, 9, tiles, frame_rate);

        auto stop_animation = Message::StopAnimation(0, end_frame);

        _dummy_backend->Receive(start_animation);

        _message_count = 0;

        int expected_channel = start_channel;

        // (end_channel - start_channel + 1) * (RASTER_TILE_DATA x tiles number + REGION_HISTOGRAM_DATA x1 + RASTER_TILE_SYNC x2) +
        // START_ANIMATION_ACK x1
        int expected_response_messages = (end_channel - start_channel + 1) * (tiles.size() + 1 + 2) + 1;

        auto t_start = std::chrono::high_resolution_clock::now();

        auto is_timeout = [&]() {
            auto t_end = std::chrono::high_resolution_clock::now();
            auto dt = std::chrono::duration_cast<std::chrono::seconds>(t_end - t_start).count();
            if (dt > 10) {
                spdlog::error("Animation timeout: can not receive all data messages within 10 seconds.");
                return true;
            }
            return false;
        };

        while (true) {
            while (!_dummy_backend->TryPopMessagesQueue(_message_pair)) { // wait for the data stream
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                if (is_timeout()) {
                    break;
                }
            }
            if (is_timeout()) {
                break;
            }

            ++_message_count;
            std::vector<char> message = _message_pair.first;
            auto event_type = Message::EventType(message);

            if (event_type == CARTA::EventType::RASTER_TILE_SYNC) {
                CARTA::RasterTileSync raster_tile_sync = Message::DecodeMessage<CARTA::RasterTileSync>(message);
                if (raster_tile_sync.end_sync()) {
                    int sync_channel = raster_tile_sync.channel();
                    EXPECT_DOUBLE_EQ(sync_channel, expected_channel); // received image channels should be in sequence
                    expected_channel += delta_channel;

                    int sync_stokes = raster_tile_sync.stokes();
                    auto animation_flow_control = Message::AnimationFlowControl(0, std::make_pair(sync_channel, sync_stokes));
                    _dummy_backend->Receive(animation_flow_control);
                    if (sync_channel >= end_channel) {
                        _dummy_backend->Receive(stop_animation); // stop the animation
                        break;
                    }
                }
            }
        }

        _dummy_backend->WaitForJobFinished();

        EXPECT_EQ(_message_count, expected_response_messages);

        // Play animation backward

        first_channel = 9;
        start_channel = 19;
        end_channel = 10;
        last_channel = 19;
        delta_channel = -1;

        first_frame = std::make_pair(first_channel, stokes);
        start_frame = std::make_pair(start_channel, stokes);
        last_frame = std::make_pair(last_channel, stokes);
        delta_frame = std::make_pair(delta_channel, stokes);

        start_animation = Message::StartAnimation(
            0, first_frame, start_frame, last_frame, delta_frame, CARTA::CompressionType::ZFP, 9, tiles, frame_rate);

        end_frame = std::make_pair(end_channel, stokes);

        stop_animation = Message::StopAnimation(0, end_frame);

        _dummy_backend->Receive(start_animation);

        _message_count = 0;

        expected_channel = start_channel;
        expected_response_messages = (start_channel - end_channel + 1) * (tiles.size() + 1 + 2) + 1;

        t_start = std::chrono::high_resolution_clock::now();

        while (true) {
            while (!_dummy_backend->TryPopMessagesQueue(_message_pair)) { // wait for the data stream
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                if (is_timeout()) {
                    break;
                }
            }
            if (is_timeout()) {
                break;
            }

            ++_message_count;
            std::vector<char> message = _message_pair.first;
            auto event_type = Message::EventType(message);

            if (event_type == CARTA::EventType::RASTER_TILE_SYNC) {
                CARTA::RasterTileSync raster_tile_sync = Message::DecodeMessage<CARTA::RasterTileSync>(message);
                if (raster_tile_sync.end_sync()) {
                    int sync_channel = raster_tile_sync.channel();
                    EXPECT_DOUBLE_EQ(sync_channel, expected_channel); // received image channels should be in sequence
                    expected_channel += delta_channel;

                    int sync_stokes = raster_tile_sync.stokes();
                    auto animation_flow_control = Message::AnimationFlowControl(0, std::make_pair(sync_channel, sync_stokes));
                    _dummy_backend->Receive(animation_flow_control);
                    if (sync_channel <= end_channel) {
                        _dummy_backend->Receive(stop_animation); // stop the animation
                        break;
                    }
                }
            }
        }

        _dummy_backend->WaitForJobFinished();

        EXPECT_EQ(_message_count, expected_response_messages);
    }

    void RegionRegister() {
        // Generate a FITS image
        auto filename_path_string = ImageGenerator::GeneratedFitsImagePath("640 800 25 1");
        std::filesystem::path filename_path(filename_path_string);

        CARTA::OpenFile open_file =
            Message::OpenFile(filename_path.parent_path(), filename_path.filename(), "0", 0, CARTA::RenderMode::RASTER);

        _dummy_backend->Receive(open_file);

        _dummy_backend->ClearMessagesQueue();

        auto set_region = Message::SetRegion(0, -1, CARTA::RegionType::RECTANGLE, {Message::Point(197, 489), Message::Point(10, 10)}, 0.0);

        _dummy_backend->Receive(set_region);

        _message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(_message_pair)) {
            std::vector<char> message = _message_pair.first;
            auto event_type = Message::EventType(message);
            if (event_type == CARTA::EventType::SET_REGION_ACK) {
                auto set_region_ack = Message::DecodeMessage<CARTA::SetRegionAck>(message);
                EXPECT_EQ(set_region_ack.region_id(), 1);
            }
            ++_message_count;
        }

        EXPECT_EQ(_message_count, 1);

        set_region = Message::SetRegion(0, -1, CARTA::RegionType::RECTANGLE, {Message::Point(306, 670), Message::Point(20, 48)}, 27);

        _dummy_backend->Receive(set_region);

        _message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(_message_pair)) {
            std::vector<char> message = _message_pair.first;
            auto event_type = Message::EventType(message);
            if (event_type == CARTA::EventType::SET_REGION_ACK) {
                auto set_region_ack = Message::DecodeMessage<CARTA::SetRegionAck>(message);
                EXPECT_EQ(set_region_ack.region_id(), 2);
            }
            ++_message_count;
        }

        EXPECT_EQ(_message_count, 1);

        set_region = Message::SetRegion(0, 1, CARTA::RegionType::RECTANGLE, {Message::Point(84.0, 491.0), Message::Point(10.0, 10.0)}, 0);

        _dummy_backend->Receive(set_region);

        _message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(_message_pair)) {
            std::vector<char> message = _message_pair.first;
            auto event_type = Message::EventType(message);
            if (event_type == CARTA::EventType::SET_REGION_ACK) {
                auto set_region_ack = Message::DecodeMessage<CARTA::SetRegionAck>(message);
                EXPECT_EQ(set_region_ack.region_id(), 1);
            }
            ++_message_count;
        }

        EXPECT_EQ(_message_count, 1);

        _dummy_backend->WaitForJobFinished();
    }
};

TEST_F(IcdTest, AccessCartaDefault) {
    AccessCarta(0, "", 5, CARTA::SessionType::NEW, true);
}

TEST_F(IcdTest, AccessCartaKnownDefault) {
    AccessCarta(9999, "", 5, CARTA::SessionType::RESUMED, true);
}

TEST_F(IcdTest, AccessCartaNoClientFeature) {
    AccessCarta(0, "", 0, CARTA::SessionType::NEW, true);
}

TEST_F(IcdTest, AccessCartaSameIdTwice) {
    AccessCarta(12345, "", 5, CARTA::SessionType::RESUMED, true);
    AccessCarta(12345, "", 5, CARTA::SessionType::RESUMED, true);
}

TEST_F(IcdTest, AnimatorDataStream) {
    AnimatorDataStream();
}

TEST_F(IcdTest, AnimatorNavigation) {
    AnimatorNavigation();
}

TEST_F(IcdTest, AnimatorPlayback) {
    AnimatorPlayback();
}

TEST_F(IcdTest, RegionRegister) {
    RegionRegister();
}
