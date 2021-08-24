/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "BackendTester.h"

using namespace std;

class TestAnimatorPlayback : public BackendTester {
public:
    TestAnimatorPlayback() {}
    ~TestAnimatorPlayback() = default;

    void AnimatorPlayback() {
        // Check the existence of the sample image
        if (!FileExists(LargeImagePath("M17_SWex.image"))) {
            return;
        }

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

        CARTA::OpenFile open_file = GetOpenFile(LargeImagePath(""), "M17_SWex.image", "0", 0, CARTA::RenderMode::RASTER);

        _dummy_backend->Receive(open_file);

        message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            auto event_type = GetEventType(message);
            LogResponsiveEventType(event_type);
            ++message_count;
        }

        EXPECT_EQ(message_count, 2);

        std::vector<float> tiles = {33558529.0, 33558528.0, 33562625.0, 33554433.0, 33562624.0, 33558530.0, 33554432.0, 33562626.0,
            33554434.0, 33566721.0, 33566720.0, 33566722.0};

        auto add_required_tiles = GetAddRequiredTiles(0, CARTA::CompressionType::ZFP, 11, tiles);

        _dummy_backend->Receive(add_required_tiles);

        auto set_cursor = GetSetCursor(0, 1, 1);

        _dummy_backend->Receive(set_cursor);

        auto set_spatial_requirements = GetSetSpatialRequirements(0, 0);

        _dummy_backend->Receive(set_spatial_requirements);

        _dummy_backend->WaitForJobFinished();

        message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            auto event_type = GetEventType(message);
            LogResponsiveEventType(event_type);
            ++message_count;
        }

        EXPECT_EQ(message_count, 16);

        int first_channel(0);
        int start_channel(1);
        int last_channel(24);
        int delta_channel(1);
        std::pair<int32_t, int32_t> first_frame = std::make_pair(first_channel, 0);
        std::pair<int32_t, int32_t> start_frame = std::make_pair(start_channel, 0);
        std::pair<int32_t, int32_t> last_frame = std::make_pair(last_channel, 0);
        std::pair<int32_t, int32_t> delta_frame = std::make_pair(delta_channel, 0);

        tiles = {33554432.0, 33558528.0, 33562624.0, 33566720.0, 33554433.0, 33558529.0, 33562625.0, 33566721.0, 33554434.0, 33558530.0,
            33562626.0, 33566722.0};

        auto start_animation =
            GetStartAnimation(0, first_frame, start_frame, last_frame, delta_frame, CARTA::CompressionType::ZFP, 9, tiles);

        _dummy_backend->Receive(start_animation);

        message_count = 0;

        bool stop(false);
        int expected_response_messages = (last_channel - first_channel) * (tiles.size() + 2 + 2) + 1;

        while (!stop) {
            while (!_dummy_backend->TryPopMessagesQueue(message_pair)) {
            }
            std::vector<char> message = message_pair.first;
            auto event_type = GetEventType(message);
            LogResponsiveEventType(event_type);

            if (event_type == CARTA::EventType::RASTER_TILE_SYNC) {
                CARTA::RasterTileSync raster_tile_sync = DecodeMessage<CARTA::RasterTileSync>(message);
                if (raster_tile_sync.end_sync()) {
                    int channel = raster_tile_sync.channel();
                    int stokes = raster_tile_sync.stokes();
                    auto animation_flow_control = GetAnimationFlowControl(0, std::make_pair(channel, stokes));
                    _dummy_backend->Receive(animation_flow_control);
                    if (channel == last_channel) {
                        stop = true;
                    }
                }
            }
            ++message_count;
        }

        EXPECT_EQ(message_count, expected_response_messages);
    }
};

TEST_F(TestAnimatorPlayback, ANIMATOR_PLAYBACK) {
    AnimatorPlayback();
}
