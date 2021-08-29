/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "BackendTester.h"

class TestPerCubeHistogram : public BackendTester {
public:
    TestPerCubeHistogram() {}
    ~TestPerCubeHistogram() = default;

    void PerCubeHistogram() {
        // Generate a FITS image
        auto filename_path_string = ImageGenerator::GeneratedFitsImagePath("640 800 250 1");
        std::filesystem::path filename_path(filename_path_string);

        CARTA::RegisterViewer register_viewer = GetRegisterViewer(0, "", 5);

        _dummy_backend->Receive(register_viewer);

        CARTA::CloseFile close_file = GetCloseFile(-1);

        _dummy_backend->Receive(close_file);

        CARTA::OpenFile open_file = GetOpenFile(filename_path.parent_path(), filename_path.filename(), "0", 0, CARTA::RenderMode::RASTER);

        _dummy_backend->Receive(open_file);

        _dummy_backend->ClearMessagesQueue();

        CARTA::SetHistogramRequirements histogram_requirements = GetSetHistogramRequirements(0, -2, -2, 1);

        _dummy_backend->Receive(histogram_requirements);

        _dummy_backend->WaitForJobFinished();

        std::atomic<int> message_count = 0;
        std::pair<std::vector<char>, bool> message_pair;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            CARTA::EventType event_type = GetEventType(message);
            LogResponsiveEventType(event_type);

            if (event_type == CARTA::EventType::REGION_HISTOGRAM_DATA) {
                auto region_histogram_data = DecodeMessage<CARTA::RegionHistogramData>(message);
                EXPECT_LE(region_histogram_data.progress(), 1);
                EXPECT_TRUE(region_histogram_data.has_histograms());
                if (region_histogram_data.has_histograms() && region_histogram_data.progress() > 0.5) {
                    auto histograms = region_histogram_data.histograms();
                    EXPECT_GT(histograms.bins_size(), 0);
                }
                ++message_count;
            }
        }

        EXPECT_GT(message_count, 0); // report the progress and partial/all data at least one time
    }
};

TEST_F(TestPerCubeHistogram, PER_CUBE_HISTOGRAM) {
    PerCubeHistogram();
}
