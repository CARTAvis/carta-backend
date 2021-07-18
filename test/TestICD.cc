/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "DummyBackend.h"

using namespace std;

class ICDTest : public ::testing::Test {
public:
    ICDTest() {
        _dummy_backend = std::make_unique<DummyBackend>();
    }

    ~ICDTest() = default;

    void TestOnRegisterViewer(uint32_t session_id, string api_key, uint32_t client_feature_flags, CARTA::SessionType expected_session_type,
        bool expected_message) {
        CARTA::RegisterViewer message;
        message.set_session_id(session_id);
        message.set_api_key(api_key);
        message.set_client_feature_flags(client_feature_flags);

        auto t_start = std::chrono::high_resolution_clock::now();

        _dummy_backend->ReceiveMessage(message);

        auto t_end = std::chrono::high_resolution_clock::now();
        auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();
        EXPECT_LT(dt, 100); // expect the process time within 100 ms

        _dummy_backend->CheckRegisterViewerAck(session_id, expected_session_type, expected_message);
    }

private:
    std::unique_ptr<DummyBackend> _dummy_backend;
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
