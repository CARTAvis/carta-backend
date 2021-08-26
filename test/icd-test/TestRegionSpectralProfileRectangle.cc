/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "BackendTester.h"

using namespace std;

class TestRegionSpectralProfileRectangle : public BackendTester {
public:
    TestRegionSpectralProfileRectangle() {}
    ~TestRegionSpectralProfileRectangle() = default;

    void RegionSpectralProfileRectangle() {
        // Check the existence of the sample image
        if (!FileExists(LargeImagePath("M17_SWex.image"))) {
            return;
        }

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

        CARTA::OpenFile open_file = GetOpenFile(LargeImagePath(""), "M17_SWex.image", "0", 0, CARTA::RenderMode::RASTER);

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
                    double values[tmp.size() / sizeof(double)];
                    std::memcpy(values, tmp.data(), tmp.size());

                    if (spectral_profile_data.profiles(i).stats_type() == CARTA::StatsType::Sum) {
                        EXPECT_DOUBLE_EQ(values[10], 0.86641662567853928);
                    }
                    if (spectral_profile_data.profiles(i).stats_type() == CARTA::StatsType::FluxDensity) {
                        EXPECT_DOUBLE_EQ(values[10], 0.039805308044335706);
                    }
                    if (spectral_profile_data.profiles(i).stats_type() == CARTA::StatsType::Mean) {
                        EXPECT_DOUBLE_EQ(values[10], 0.057761108378569286);
                    }
                    if (spectral_profile_data.profiles(i).stats_type() == CARTA::StatsType::RMS) {
                        EXPECT_DOUBLE_EQ(values[10], 0.05839547505408027);
                    }
                    if (spectral_profile_data.profiles(i).stats_type() == CARTA::StatsType::Sigma) {
                        EXPECT_DOUBLE_EQ(values[10], 0.0088853315888891247);
                    }
                    if (spectral_profile_data.profiles(i).stats_type() == CARTA::StatsType::SumSq) {
                        EXPECT_DOUBLE_EQ(values[10], 0.051150472601875663);
                    }
                    if (spectral_profile_data.profiles(i).stats_type() == CARTA::StatsType::Min) {
                        EXPECT_DOUBLE_EQ(values[10], 0.03859434649348259);
                    }
                    if (spectral_profile_data.profiles(i).stats_type() == CARTA::StatsType::Max) {
                        EXPECT_DOUBLE_EQ(values[10], 0.070224300026893616);
                    }
                    if (spectral_profile_data.profiles(i).stats_type() == CARTA::StatsType::Extrema) {
                        EXPECT_DOUBLE_EQ(values[10], 0.070224300026893616);
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
