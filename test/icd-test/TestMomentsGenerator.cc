/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "BackendTester.h"

class TestMomentsGenerator : public BackendTester {
public:
    TestMomentsGenerator() {}
    ~TestMomentsGenerator() = default;

    void MomentsGenerator() {
        // Generate a FITS image
        auto filename_path_string = ImageGenerator::GeneratedFitsImagePath("640 800 25 1");
        std::filesystem::path filename_path(filename_path_string);

        CARTA::RegisterViewer register_viewer = GetRegisterViewer(0, "", 5);

        _dummy_backend->Receive(register_viewer);

        // Resulting message
        std::pair<std::vector<char>, bool> message_pair;

        CARTA::CloseFile close_file = GetCloseFile(-1);

        _dummy_backend->Receive(close_file);

        CARTA::OpenFile open_file = GetOpenFile(filename_path.parent_path(), filename_path.filename(), "0", 0, CARTA::RenderMode::RASTER);

        _dummy_backend->Receive(open_file);

        _dummy_backend->ClearMessagesQueue();

        CARTA::MomentRequest moment_request =
            GetMomentsRequest(0, 0, CARTA::MomentAxis::SPECTRAL, CARTA::MomentMask::Include, GetIntBounds(0, 24), GetFloatBounds(-1, 1));

        _dummy_backend->Receive(moment_request);

        _dummy_backend->WaitForJobFinished();

        std::atomic<int> message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            CARTA::EventType event_type = GetEventType(message);
            LogSentEventType(event_type);

            if (event_type == CARTA::EventType::MOMENT_RESPONSE) {
                auto moment_response = DecodeMessage<CARTA::MomentResponse>(message);
                EXPECT_TRUE(moment_response.success());
                EXPECT_EQ(moment_response.open_file_acks_size(), 12);
                ++message_count;
            }
        }

        EXPECT_EQ(message_count, 1);
    }
};

TEST_F(TestMomentsGenerator, MOMENTS_GENERATOR) {
    MomentsGenerator();
}
