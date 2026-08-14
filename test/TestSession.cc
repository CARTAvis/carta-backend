/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <algorithm>

#include <gtest/gtest.h>

#include "CommonTestUtilities.h"
#include "DataStream/Tile.h"
#include "ImageData/FileLoader.h"
#include "Session/Session.h"

using namespace carta;

class ChannelMapTestSession : public Session {
public:
    ChannelMapTestSession(fs::path image = FitsImages() / "10x10x10.fits") : Session(nullptr, nullptr, 0, "", nullptr) {
        auto loader = FileLoader::GetLoader(image);
        _frames.emplace(FILE_ID, std::make_shared<Frame>(0, loader, "0"));
    }

    int CurrentChannel() const {
        return _frames.at(FILE_ID)->CurrentZ();
    }

    int CurrentStokes() const {
        return _frames.at(FILE_ID)->CurrentStokes();
    }

    int Depth() const {
        return _frames.at(FILE_ID)->Depth();
    }

    std::vector<EventHeader> TakeOutgoingEventHeaders() {
        std::vector<EventHeader> headers;
        std::pair<std::vector<char>, bool> message;
        while (_out_msgs.try_pop(message)) {
            headers.push_back(Message::GetEventHeader(std::string_view(message.first.data(), message.first.size())));
        }
        return headers;
    }

    std::vector<std::pair<EventHeader, CARTA::ChannelMapFlowControl>> TakeFlowControlEvents() {
        std::vector<std::pair<EventHeader, CARTA::ChannelMapFlowControl>> events;
        std::pair<std::vector<char>, bool> message;
        while (_out_msgs.try_pop(message)) {
            std::string_view message_view(message.first.data(), message.first.size());
            auto header = Message::GetEventHeader(message_view);
            if (header.GetType() == CARTA::EventType::CHANNEL_MAP_FLOW_CONTROL) {
                events.emplace_back(header, Message::DecodeMessage<CARTA::ChannelMapFlowControl>(message_view));
            }
        }
        return events;
    }

    std::vector<CARTA::RasterTileData> TakeRasterTileData() {
        std::vector<CARTA::RasterTileData> tiles;
        std::pair<std::vector<char>, bool> message;
        while (_out_msgs.try_pop(message)) {
            std::string_view message_view(message.first.data(), message.first.size());
            if (Message::GetEventHeader(message_view).GetType() == CARTA::EventType::RASTER_TILE_DATA) {
                tiles.push_back(Message::DecodeMessage<CARTA::RasterTileData>(message_view));
            }
        }
        return tiles;
    }

    std::vector<CARTA::RasterTileSync> TakeRasterTileSync() {
        std::vector<CARTA::RasterTileSync> sync_messages;
        std::pair<std::vector<char>, bool> message;
        while (_out_msgs.try_pop(message)) {
            std::string_view message_view(message.first.data(), message.first.size());
            if (Message::GetEventHeader(message_view).GetType() == CARTA::EventType::RASTER_TILE_SYNC) {
                sync_messages.push_back(Message::DecodeMessage<CARTA::RasterTileSync>(message_view));
            }
        }
        return sync_messages;
    }

    void EnableContours() {
        CARTA::SetContourParameters message;
        message.set_file_id(FILE_ID);
        message.set_reference_file_id(FILE_ID);
        auto* image_bounds = message.mutable_image_bounds();
        image_bounds->set_x_max(_frames.at(FILE_ID)->Width());
        image_bounds->set_y_max(_frames.at(FILE_ID)->Height());
        message.add_levels(0.5);
        message.set_smoothing_mode(CARTA::SmoothingMode::NoSmoothing);
        message.set_smoothing_factor(1);
        message.set_decimation_factor(1);
        message.set_compression_level(0);
        message.set_contour_chunk_size(100000);
        ASSERT_TRUE(_frames.at(FILE_ID)->SetContourParameters(message));
    }

    static constexpr int FILE_ID = 1;
};

class SessionChannelMapTest : public ::testing::Test {
protected:
    static CARTA::SetImageChannels ChannelRequest(int channel, bool with_tile, int stokes = 0) {
        CARTA::SetImageChannels message;
        message.set_file_id(ChannelMapTestSession::FILE_ID);
        message.set_channel(channel);
        message.set_stokes(stokes);
        message.set_channel_map_enabled(true);
        if (with_tile) {
            auto* required_tiles = message.mutable_required_tiles();
            required_tiles->set_file_id(ChannelMapTestSession::FILE_ID);
            required_tiles->add_tiles(Tile::Encode(0, 0, 0));
        }
        return message;
    }
};

TEST_F(SessionChannelMapTest, RejectsInvalidChannelDataRequests) {
    ChannelMapTestSession session;

    for (int channel : {-1, session.Depth()}) {
        session.OnSetImageChannels(ChannelRequest(channel, true));

        auto headers = session.TakeOutgoingEventHeaders();
        ASSERT_EQ(headers.size(), 1);
        EXPECT_EQ(headers.front().GetType(), CARTA::EventType::ERROR_DATA);
        EXPECT_EQ(session.CurrentChannel(), 0);
    }
}

TEST_F(SessionChannelMapTest, KeepsCurrentChannelWhileSendingChannelData) {
    ChannelMapTestSession session;
    constexpr uint32_t request_id = 42;

    session.ExecuteSetChannelEvt({ChannelRequest(1, true), request_id});

    auto events = session.TakeFlowControlEvents();
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(session.CurrentChannel(), 0);
    EXPECT_EQ(events.front().first.request_id, request_id);
    EXPECT_EQ(events.front().second.completed_channel(), 1);
    EXPECT_EQ(events.front().second.status(), CARTA::ChannelMapFlowControl::COMPLETED);
}

TEST_F(SessionChannelMapTest, UsesRequestIdForRasterTileSync) {
    ChannelMapTestSession session;
    constexpr uint32_t request_id = 42;

    session.OnSetImageChannels(ChannelRequest(1, true), request_id);

    auto headers = session.TakeOutgoingEventHeaders();
    std::vector<EventHeader> sync_headers;
    std::copy_if(headers.begin(), headers.end(), std::back_inserter(sync_headers),
        [](const auto& header) { return header.GetType() == CARTA::EventType::RASTER_TILE_SYNC; });
    ASSERT_EQ(sync_headers.size(), 2);
    EXPECT_TRUE(std::all_of(sync_headers.begin(), sync_headers.end(),
        [request_id](const auto& header) { return header.request_id == request_id; }));
}

TEST_F(SessionChannelMapTest, RejectsInvalidChannelMapRequests) {
    ChannelMapTestSession session;

    session.ExecuteSetChannelEvt({ChannelRequest(session.Depth(), true), 42});

    auto events = session.TakeFlowControlEvents();
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events.front().second.status(), CARTA::ChannelMapFlowControl::REJECTED);
    EXPECT_FALSE(events.front().second.message().empty());
}

TEST_F(SessionChannelMapTest, RejectsRequestsForMissingFiles) {
    ChannelMapTestSession session;
    auto request = ChannelRequest(1, true);
    request.set_file_id(99);

    session.ExecuteSetChannelEvt({request, 42});

    auto events = session.TakeFlowControlEvents();
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events.front().second.status(), CARTA::ChannelMapFlowControl::REJECTED);
}

TEST_F(SessionChannelMapTest, CompletesIncompleteTileGenerationWithSuccessfulCount) {
    ChannelMapTestSession session;
    auto request = ChannelRequest(1, true);
    request.mutable_required_tiles()->add_tiles(Tile::Encode(0, 0, 0));
    request.mutable_required_tiles()->set_tiles(0, Tile::Encode(0, 0, 1));

    auto result = session.OnSetImageChannels(request);

    EXPECT_EQ(result.status, CARTA::ChannelMapFlowControl::COMPLETED);
    auto sync_messages = session.TakeRasterTileSync();
    ASSERT_EQ(sync_messages.size(), 2);
    EXPECT_EQ(sync_messages.front().tile_count(), 2);
    EXPECT_EQ(sync_messages.back().tile_count(), 1);
    EXPECT_TRUE(sync_messages.back().end_sync());
}

TEST_F(SessionChannelMapTest, ChannelDataRequestsDoNotGenerateOverlays) {
    ChannelMapTestSession session;
    session.EnableContours();

    session.OnSetImageChannels(ChannelRequest(1, true));

    auto headers = session.TakeOutgoingEventHeaders();
    EXPECT_EQ(std::count_if(headers.begin(), headers.end(),
                  [](const auto& header) { return header.GetType() == CARTA::EventType::CONTOUR_IMAGE_DATA; }),
        0);
    EXPECT_EQ(std::count_if(headers.begin(), headers.end(),
                  [](const auto& header) { return header.GetType() == CARTA::EventType::VECTOR_OVERLAY_TILE_DATA; }),
        0);
}

TEST_F(SessionChannelMapTest, CancelsSupersededQueuedRequests) {
    ChannelMapTestSession session;

    session.AddToSetChannelQueue(ChannelRequest(1, true), 41);
    session.AddToSetChannelQueue(ChannelRequest(2, true), 42);

    auto events = session.TakeFlowControlEvents();
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events.front().first.request_id, 41);
    EXPECT_EQ(events.front().second.completed_channel(), 1);
    EXPECT_EQ(events.front().second.status(), CARTA::ChannelMapFlowControl::CANCELLED);
}

TEST_F(SessionChannelMapTest, UsesRequestedStokesWithoutChangingCurrentStokes) {
    ChannelMapTestSession session(FitsImages() / "noise_4d.fits");

    session.OnSetImageChannels(ChannelRequest(0, true, 1));

    auto raster_tiles = session.TakeRasterTileData();
    ASSERT_EQ(raster_tiles.size(), 1);
    EXPECT_EQ(raster_tiles.front().stokes(), 1);
    EXPECT_EQ(session.CurrentStokes(), 0);
}

TEST_F(SessionChannelMapTest, EmptyRequestUpdatesCurrentChannel) {
    ChannelMapTestSession session;

    session.OnSetImageChannels(ChannelRequest(1, false));

    EXPECT_EQ(session.CurrentChannel(), 1);
}
