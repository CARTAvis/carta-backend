/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "CommonTestUtilities.h"
#include "DataStream/Tile.h"
#include "ImageData/FileLoader.h"
#include "Session/Session.h"

using namespace carta;

class ChannelMapTestSession : public Session {
public:
    ChannelMapTestSession() : Session(nullptr, nullptr, 0, "", nullptr) {
        auto loader = FileLoader::GetLoader(FitsImages() / "10x10x10.fits");
        _frames.emplace(FILE_ID, std::make_shared<Frame>(0, loader, "0"));
    }

    int CurrentChannel() const {
        return _frames.at(FILE_ID)->CurrentZ();
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

    static constexpr int FILE_ID = 1;
};

class SessionChannelMapTest : public ::testing::Test {
protected:
    static CARTA::SetImageChannels ChannelRequest(int channel, bool with_tile) {
        CARTA::SetImageChannels message;
        message.set_file_id(ChannelMapTestSession::FILE_ID);
        message.set_channel(channel);
        message.set_stokes(0);
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

    auto headers = session.TakeOutgoingEventHeaders();
    ASSERT_FALSE(headers.empty());
    EXPECT_EQ(session.CurrentChannel(), 0);
    EXPECT_EQ(headers.back().GetType(), CARTA::EventType::CHANNEL_MAP_FLOW_CONTROL);
    EXPECT_EQ(headers.back().request_id, request_id);
}

TEST_F(SessionChannelMapTest, EmptyRequestUpdatesCurrentChannel) {
    ChannelMapTestSession session;

    session.OnSetImageChannels(ChannelRequest(1, false));

    EXPECT_EQ(session.CurrentChannel(), 1);
}
