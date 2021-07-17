/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "Logger/Logger.h"
#include "Session.h"
#include "Threading.h"

using namespace std;

class ICDTest : public ::testing::Test {
public:
    ICDTest() {
        uint32_t session_id(0);
        std::string address;
        std::string top_level_folder("/");
        std::string starting_folder("data/images");
        int grpc_port(-1);

        _file_list_handler = new FileListHandler(top_level_folder, starting_folder);
        _session = new Session(nullptr, nullptr, session_id, address, top_level_folder, starting_folder, _file_list_handler, grpc_port);
    }

    ~ICDTest() {
        delete _session;
        delete _file_list_handler;
    }

    void TestOnRegisterViewer(uint32_t session_id, string api_key, uint32_t client_feature_flags, CARTA::SessionType expected_session_type,
        bool expected_message) {
        CARTA::RegisterViewer message;
        message.set_session_id(session_id);
        message.set_api_key(api_key);
        message.set_client_feature_flags(client_feature_flags);

        uint16_t icd_version(ICD_VERSION);
        uint32_t request_id(0);

        auto t_start = std::chrono::high_resolution_clock::now();

        _session->OnRegisterViewer(message, icd_version, request_id);

        _session->CheckMessagesQueue([=](tbb::concurrent_queue<std::pair<std::vector<char>, bool>> messages_queue) {
            std::pair<std::vector<char>, bool> messages_pair;

            while (messages_queue.try_pop(messages_pair)) {
                auto t_end = std::chrono::high_resolution_clock::now();
                auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();
                EXPECT_LT(dt, 100); // expect to receive the message within 100 ms

                std::vector<char> message = messages_pair.first;
                carta::EventHeader head = *reinterpret_cast<const carta::EventHeader*>(message.data());

                EXPECT_EQ(head.type, CARTA::EventType::REGISTER_VIEWER_ACK);
                EXPECT_EQ(head.request_id, request_id);
                EXPECT_EQ(head.icd_version, icd_version);

                CARTA::RegisterViewerAck register_viewer_ack;
                char* event_buf = message.data() + sizeof(carta::EventHeader);
                int event_length = message.size() - sizeof(carta::EventHeader);
                register_viewer_ack.ParseFromArray(event_buf, event_length);

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
        });
    }

private:
    FileListHandler* _file_list_handler;
    Session* _session;
};

TEST_F(ICDTest, ACCESS_CARTA_DEFAULT) {
    TestOnRegisterViewer(0, "", 5, CARTA::SessionType::NEW, true);
}

TEST_F(ICDTest, ACCESS_CARTA_KNOWN_DEFAULT) {
    TestOnRegisterViewer(9999, "", 5, CARTA::SessionType::RESUMED, true);
}

TEST_F(ICDTest, ACCESS_CARTA_NO_CLIENT_FEATURE) {
    TestOnRegisterViewer(0, "", 0, CARTA::SessionType::NEW, true);
}

TEST_F(ICDTest, ACCESS_CARTA_SAME_ID_TWICE) {
    TestOnRegisterViewer(12345, "", 5, CARTA::SessionType::RESUMED, true);
    TestOnRegisterViewer(12345, "", 5, CARTA::SessionType::RESUMED, true);
}
