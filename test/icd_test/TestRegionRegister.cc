/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "BackendTester.h"

using namespace std;

class TestRegionRegister : public BackendTester {
public:
    TestRegionRegister() {}
    ~TestRegionRegister() = default;

    void RegionRegister() {
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

        auto add_required_tiles = GetAddRequiredTiles(0, CARTA::CompressionType::ZFP, 11, std::vector<float>{0});

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

        auto set_region = GetSetRegion(0, -1, CARTA::RegionType::RECTANGLE, {GetPoint(197, 489), GetPoint(10, 10)}, 0.0);

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

        set_region = GetSetRegion(0, -1, CARTA::RegionType::RECTANGLE, {GetPoint(306, 670), GetPoint(20, 48)}, 27);

        _dummy_backend->ReceiveMessage(set_region);

        message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            auto event_type = GetEventType(message);
            LogResponseEventType(event_type);
            if (event_type == CARTA::EventType::SET_REGION_ACK) {
                auto set_region_ack = DecodeMessage<CARTA::SetRegionAck>(message);
                EXPECT_EQ(set_region_ack.region_id(), 2);
            }
            ++message_count;
        }

        EXPECT_EQ(message_count, 1);

        set_region = GetSetRegion(0, -1, CARTA::RegionType::ELLIPSE, {GetPoint(551.0, 330.0), GetPoint(30.0, 15.0)}, 0);

        _dummy_backend->ReceiveMessage(set_region);

        message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            auto event_type = GetEventType(message);
            LogResponseEventType(event_type);
            if (event_type == CARTA::EventType::SET_REGION_ACK) {
                auto set_region_ack = DecodeMessage<CARTA::SetRegionAck>(message);
                EXPECT_EQ(set_region_ack.region_id(), 3);
            }
            ++message_count;
        }

        EXPECT_EQ(message_count, 1);

        set_region = GetSetRegion(0, -1, CARTA::RegionType::RECTANGLE, {GetPoint(580.0, 240.0), GetPoint(35.0, 35.0)}, 0);

        _dummy_backend->ReceiveMessage(set_region);

        message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            auto event_type = GetEventType(message);
            LogResponseEventType(event_type);
            if (event_type == CARTA::EventType::SET_REGION_ACK) {
                auto set_region_ack = DecodeMessage<CARTA::SetRegionAck>(message);
                EXPECT_EQ(set_region_ack.region_id(), 4);
            }
            ++message_count;
        }

        EXPECT_EQ(message_count, 1);

        set_region = GetSetRegion(0, -1, CARTA::RegionType::RECTANGLE, {GetPoint(552.0, 184.0), GetPoint(350.0, 18.0)}, 0);

        _dummy_backend->ReceiveMessage(set_region);

        message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            auto event_type = GetEventType(message);
            LogResponseEventType(event_type);
            if (event_type == CARTA::EventType::SET_REGION_ACK) {
                auto set_region_ack = DecodeMessage<CARTA::SetRegionAck>(message);
                EXPECT_EQ(set_region_ack.region_id(), 5);
            }
            ++message_count;
        }

        EXPECT_EQ(message_count, 1);

        set_region = GetSetRegion(0, -1, CARTA::RegionType::RECTANGLE, {GetPoint(635.0, 128.0), GetPoint(25.0, 48.0)}, 0);

        _dummy_backend->ReceiveMessage(set_region);

        message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            auto event_type = GetEventType(message);
            LogResponseEventType(event_type);
            if (event_type == CARTA::EventType::SET_REGION_ACK) {
                auto set_region_ack = DecodeMessage<CARTA::SetRegionAck>(message);
                EXPECT_EQ(set_region_ack.region_id(), 6);
            }
            ++message_count;
        }

        EXPECT_EQ(message_count, 1);

        set_region = GetSetRegion(0, -1, CARTA::RegionType::RECTANGLE, {GetPoint(694.0, 80.0), GetPoint(25.0, 33.0)}, 0);

        _dummy_backend->ReceiveMessage(set_region);

        message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            auto event_type = GetEventType(message);
            LogResponseEventType(event_type);
            if (event_type == CARTA::EventType::SET_REGION_ACK) {
                auto set_region_ack = DecodeMessage<CARTA::SetRegionAck>(message);
                EXPECT_EQ(set_region_ack.region_id(), 7);
            }
            ++message_count;
        }

        EXPECT_EQ(message_count, 1);

        set_region = GetSetRegion(0, 1, CARTA::RegionType::RECTANGLE, {GetPoint(84.0, 491.0), GetPoint(10.0, 10.0)}, 0);

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

        set_region = GetSetRegion(0, 1, CARTA::RegionType::RECTANGLE, {GetPoint(43.0, 491.0), GetPoint(10.0, 10.0)}, 0);

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

        set_region = GetSetRegion(0, 1, CARTA::RegionType::RECTANGLE, {GetPoint(-1.0, 491.0), GetPoint(10.0, 10.0)}, 0);

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

        set_region = GetSetRegion(0, 1, CARTA::RegionType::RECTANGLE, {GetPoint(-14.0, 491.0), GetPoint(10.0, 10.0)}, 0);

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

        set_region = GetSetRegion(0, 1, CARTA::RegionType::RECTANGLE, {GetPoint(197.0, 489.0), GetPoint(10.0, 10.0)}, 0);

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
    }
};

TEST_F(TestRegionRegister, REGION_REGISTER) {
    RegionRegister();
}
