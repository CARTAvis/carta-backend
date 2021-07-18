/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "DummyBackend.h"

#include <gtest/gtest.h>

static const uint16_t DUMMY_ICD_VERSION(ICD_VERSION);
static const uint32_t DUMMY_REQUEST_ID(0);

DummyBackend::DummyBackend() {
    uint32_t session_id(0);
    std::string address;
    std::string top_level_folder("/");
    std::string starting_folder("data/images");
    int grpc_port(-1);

    _file_list_handler = new FileListHandler(top_level_folder, starting_folder);
    _session = new Session(nullptr, nullptr, session_id, address, top_level_folder, starting_folder, _file_list_handler, grpc_port);
}

DummyBackend::~DummyBackend() {
    delete _session;
    delete _file_list_handler;
}

void DummyBackend::ReceiveMessage(CARTA::RegisterViewer message) {
    _session->OnRegisterViewer(message, DUMMY_ICD_VERSION, DUMMY_REQUEST_ID);
}

void DummyBackend::CheckRegisterViewerAck(uint32_t expected_session_id, CARTA::SessionType expected_session_type, bool expected_message) {
    _session->CheckMessagesQueue([=](tbb::concurrent_queue<std::pair<std::vector<char>, bool>> messages_queue) {
        std::pair<std::vector<char>, bool> messages_pair;
        while (messages_queue.try_pop(messages_pair)) {
            std::vector<char> message = messages_pair.first;
            carta::EventHeader head = *reinterpret_cast<const carta::EventHeader*>(message.data());
            EXPECT_EQ(head.type, CARTA::EventType::REGISTER_VIEWER_ACK);

            if (head.type == CARTA::EventType::REGISTER_VIEWER_ACK) {
                CARTA::RegisterViewerAck register_viewer_ack;
                char* event_buf = message.data() + sizeof(carta::EventHeader);
                int event_length = message.size() - sizeof(carta::EventHeader);
                register_viewer_ack.ParseFromArray(event_buf, event_length);

                EXPECT_TRUE(register_viewer_ack.success());
                EXPECT_EQ(register_viewer_ack.session_id(), expected_session_id);
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
    });
}
