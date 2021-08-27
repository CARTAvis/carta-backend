/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "BackendTester.h"

class TestRegionSpectralProfileRectangle : public BackendTester {
public:
    TestRegionSpectralProfileRectangle() {}
    ~TestRegionSpectralProfileRectangle() = default;

    void RegionSpectralProfileRectangle() {
        auto filename_path_string = ImageGenerator::GeneratedFitsImagePath("640 800 25 1");
        std::filesystem::path filename_path(filename_path_string);

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

        CARTA::OpenFile open_file = GetOpenFile(filename_path.parent_path(), filename_path.filename(), "0", 0, CARTA::RenderMode::RASTER);

        _dummy_backend->Receive(open_file);

        message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            auto event_type = GetEventType(message);
            LogResponsiveEventType(event_type);
            ++message_count;
        }

        EXPECT_EQ(message_count, 2);

        auto add_required_tiles = GetAddRequiredTiles(0, CARTA::CompressionType::ZFP, 11, std::vector<float>{0});

        _dummy_backend->Receive(add_required_tiles);
        _dummy_backend->WaitForJobFinished();

        auto set_cursor = GetSetCursor(0, 1, 1);

        _dummy_backend->Receive(set_cursor);
        _dummy_backend->WaitForJobFinished();

        message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            auto event_type = GetEventType(message);
            LogResponsiveEventType(event_type);
            ++message_count;
        }

        EXPECT_EQ(message_count, 4);

        auto set_region = GetSetRegion(0, -1, CARTA::RegionType::RECTANGLE, {GetPoint(83, 489), GetPoint(4, 6)}, 0);

        _dummy_backend->Receive(set_region);

        message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            auto event_type = GetEventType(message);
            LogResponsiveEventType(event_type);
            if (event_type == CARTA::EventType::SET_REGION_ACK) {
                auto set_region_ack = DecodeMessage<CARTA::SetRegionAck>(message);
                EXPECT_EQ(set_region_ack.region_id(), 1);
            }
            ++message_count;
        }

        EXPECT_EQ(message_count, 1);

        auto set_spectral_requirements = GetSetSpectralRequirements(0, 1, "z");

        _dummy_backend->Receive(set_spectral_requirements);
        _dummy_backend->WaitForJobFinished();

        message_count = 0;

        while (_dummy_backend->TryPopMessagesQueue(message_pair)) {
            std::vector<char> message = message_pair.first;
            auto event_type = GetEventType(message);
            LogResponsiveEventType(event_type);

            if (event_type == CARTA::EventType::SPECTRAL_PROFILE_DATA) {
                auto spectral_profile_data = DecodeMessage<CARTA::SpectralProfileData>(message);
                EXPECT_EQ(spectral_profile_data.region_id(), 1);
                EXPECT_EQ(spectral_profile_data.progress(), 1);
                for (int i = 0; i < spectral_profile_data.profiles_size(); ++i) {
                    if (spectral_profile_data.profiles(i).raw_values_fp64().empty()) {
                        continue;
                    }

                    string tmp = spectral_profile_data.profiles(i).raw_values_fp64();
                    int array_size = tmp.size() / sizeof(double);
                    EXPECT_EQ(array_size, 25);
                    double values[array_size];
                    std::memcpy(values, tmp.data(), tmp.size());

                    for (auto value : values) {
                        if ((spectral_profile_data.profiles(i).stats_type() == CARTA::StatsType::NumPixels) ||
                            (spectral_profile_data.profiles(i).stats_type() == CARTA::StatsType::FluxDensity)) {
                            EXPECT_TRUE(std::isnan(value));
                        } else {
                            EXPECT_TRUE(!std::isnan(value));
                        }
                    }
                }
            }
            ++message_count;
        }

        EXPECT_EQ(message_count, 1);
    }
};

TEST_F(TestRegionSpectralProfileRectangle, REGION_SPECTRAL_PROFILE_RECTANGLE) {
    RegionSpectralProfileRectangle();
}
