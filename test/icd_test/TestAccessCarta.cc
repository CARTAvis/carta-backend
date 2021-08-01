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

class TestAccessCarta : public ::testing::Test, public FileFinder {
public:
    TestAccessCarta() : _dummy_backend(std::make_unique<DummyBackend>()) {}

    ~TestAccessCarta() = default;

    void AccessCarta(uint32_t session_id, string api_key, uint32_t client_feature_flags, CARTA::SessionType expected_session_type,
        bool expected_message) {
        CARTA::RegisterViewer register_viewer = GetRegisterViewer(session_id, api_key, client_feature_flags);

        auto t_start = std::chrono::high_resolution_clock::now();

        _dummy_backend->ReceiveMessage(register_viewer);

        auto t_end = std::chrono::high_resolution_clock::now();
        auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();
        EXPECT_LT(dt, 100); // expect the process time within 100 ms

        // Resulting message
        std::pair<std::vector<char>, bool> message_pair;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            CARTA::EventType event_type = GetEventType(message);
            EXPECT_EQ(event_type, CARTA::EventType::REGISTER_VIEWER_ACK);

            if (event_type == CARTA::EventType::REGISTER_VIEWER_ACK) {
                CARTA::RegisterViewerAck register_viewer_ack = GetRegisterViewerAck(message);

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
        }
    }

private:
    std::unique_ptr<DummyBackend> _dummy_backend;
};

TEST_F(TestAccessCarta, ACCESS_CARTA_DEFAULT) {
    AccessCarta(0, "", 5, CARTA::SessionType::NEW, true);
}

TEST_F(TestAccessCarta, ACCESS_CARTA_KNOWN_DEFAULT) {
    AccessCarta(9999, "", 5, CARTA::SessionType::RESUMED, true);
}

TEST_F(TestAccessCarta, ACCESS_CARTA_NO_CLIENT_FEATURE) {
    AccessCarta(0, "", 0, CARTA::SessionType::NEW, true);
}

TEST_F(TestAccessCarta, ACCESS_CARTA_SAME_ID_TWICE) {
    AccessCarta(12345, "", 5, CARTA::SessionType::RESUMED, true);
    AccessCarta(12345, "", 5, CARTA::SessionType::RESUMED, true);
}
