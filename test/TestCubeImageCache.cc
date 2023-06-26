/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "CommonTestUtilities.h"
#include "ImageData/FileLoader.h"
#include "Util/Message.h"
#include "src/Frame/Frame.h"

using namespace carta;

static const std::string IMAGE_OPTS = "-s 0 -n row column -d 10";
static const double ONE_MILLION = 1000000;

class CubeImageCacheTest : public ::testing::Test, public ImageGenerator {
public:
    static std::tuple<CARTA::SpatialProfile, CARTA::SpatialProfile> GetProfiles(CARTA::SpatialProfileData& data) {
        if (data.profiles(0).coordinate().back() == 'x') {
            return {data.profiles(0), data.profiles(1)};
        }
        return {data.profiles(1), data.profiles(0)};
    }

    static void Fits3DSpatialProfile(int default_channel, bool cube_image_cache) {
        auto path_string = GeneratedFitsImagePath("10 10 10", IMAGE_OPTS);
        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        int reserved_memory = cube_image_cache ? std::ceil(10 * 10 * 10 * sizeof(float) / ONE_MILLION) : 0;
        std::unique_ptr<Frame> frame(new Frame(0, loader, "0", default_channel, reserved_memory));
        FitsDataReader reader(path_string);

        int x(1), y(1);
        int channel(5);
        int stokes(0);

        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {Message::SpatialConfig("x"), Message::SpatialConfig("y")};
        frame->SetSpatialRequirements(profiles);
        std::string msg;
        frame->SetCursor(x, y);
        frame->SetImageChannels(channel, stokes, msg);

        std::vector<CARTA::SpatialProfileData> data_vec;
        frame->FillSpatialProfileData(data_vec);

        for (auto& data : data_vec) {
            EXPECT_EQ(data.file_id(), 0);
            EXPECT_EQ(data.region_id(), CURSOR_REGION_ID);
            EXPECT_EQ(data.x(), x);
            EXPECT_EQ(data.y(), y);
            EXPECT_EQ(data.channel(), channel);
            EXPECT_EQ(data.stokes(), stokes);
            CmpValues(data.value(), reader.ReadPointXY(x, y, channel));
            EXPECT_EQ(data.profiles_size(), 2);

            auto [x_profile, y_profile] = GetProfiles(data);

            EXPECT_EQ(x_profile.start(), 0);
            EXPECT_EQ(x_profile.end(), 10);
            EXPECT_EQ(x_profile.mip(), 0);
            auto x_vals = GetSpatialProfileValues(x_profile);
            EXPECT_EQ(x_vals.size(), 10);
            CmpVectors(x_vals, reader.ReadProfileX(y, channel));

            EXPECT_EQ(y_profile.start(), 0);
            EXPECT_EQ(y_profile.end(), 10);
            EXPECT_EQ(y_profile.mip(), 0);
            auto y_vals = GetSpatialProfileValues(y_profile);
            EXPECT_EQ(y_vals.size(), 10);
            CmpVectors(y_vals, reader.ReadProfileY(x, channel));
        }
    }

    static void Fits4DSpatialProfile(int default_channel, bool cube_image_cache) {
        auto path_string = GeneratedFitsImagePath("10 10 10 4", IMAGE_OPTS);
        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        int reserved_memory = cube_image_cache ? std::ceil(10 * 10 * 10 * 4 * sizeof(float) / ONE_MILLION) : 0;
        std::unique_ptr<Frame> frame(new Frame(0, loader, "0", default_channel, reserved_memory));
        FitsDataReader reader(path_string);

        int x(4), y(6);
        int channel(5);
        int stokes(2);
        int spatial_config_stokes(1); // set spatial config coordinate = {"Qx", "Qy"}

        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {Message::SpatialConfig("Qx"), Message::SpatialConfig("Qy")};
        frame->SetSpatialRequirements(profiles);
        frame->SetCursor(x, y);
        std::string msg;
        frame->SetImageChannels(channel, stokes, msg);

        std::vector<CARTA::SpatialProfileData> data_vec;
        frame->FillSpatialProfileData(data_vec);

        for (auto& data : data_vec) {
            EXPECT_EQ(data.file_id(), 0);
            EXPECT_EQ(data.region_id(), CURSOR_REGION_ID);
            EXPECT_EQ(data.x(), x);
            EXPECT_EQ(data.y(), y);
            EXPECT_EQ(data.channel(), channel);
            EXPECT_EQ(data.stokes(), spatial_config_stokes);
            CmpValues(data.value(), reader.ReadPointXY(x, y, channel, spatial_config_stokes));
            EXPECT_EQ(data.profiles_size(), 2);

            auto [x_profile, y_profile] = GetProfiles(data);

            EXPECT_EQ(x_profile.start(), 0);
            EXPECT_EQ(x_profile.end(), 10);
            EXPECT_EQ(x_profile.mip(), 0);
            auto x_vals = GetSpatialProfileValues(x_profile);
            EXPECT_EQ(x_vals.size(), 10);
            CmpVectors(x_vals, reader.ReadProfileX(y, channel, spatial_config_stokes));

            EXPECT_EQ(y_profile.start(), 0);
            EXPECT_EQ(y_profile.end(), 10);
            EXPECT_EQ(y_profile.mip(), 0);
            auto y_vals = GetSpatialProfileValues(y_profile);
            EXPECT_EQ(y_vals.size(), 10);
            CmpVectors(y_vals, reader.ReadProfileY(x, channel, spatial_config_stokes));
        }
    }

    static std::vector<float> Fits3DSpectralProfile(int default_channel, bool cube_image_cache) {
        auto path_string = GeneratedFitsImagePath("10 10 10", IMAGE_OPTS);

        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        int reserved_memory = cube_image_cache ? std::ceil(10 * 10 * 10 * sizeof(float) / ONE_MILLION) : 0;
        std::unique_ptr<Frame> frame(new Frame(0, loader, "0", default_channel, reserved_memory));

        int x(4), y(6);
        int channel(5);
        int stokes(0);
        std::string stokes_config_z("z");

        std::string msg;
        frame->SetCursor(x, y);
        frame->SetImageChannels(channel, stokes, msg);

        // Set spectral configs for the cursor
        std::vector<CARTA::SetSpectralRequirements_SpectralConfig> spectral_configs{Message::SpectralConfig(stokes_config_z)};
        frame->SetSpectralRequirements(CURSOR_REGION_ID, spectral_configs);

        // Get cursor spectral profile data from the Frame
        CARTA::SpectralProfile spectral_profile;
        bool stokes_changed = (stokes_config_z == "z");

        frame->FillSpectralProfileData(
            [&](CARTA::SpectralProfileData tmp_spectral_profile) {
                if (tmp_spectral_profile.progress() >= 1.0) {
                    spectral_profile = tmp_spectral_profile.profiles(0);
                }
            },
            CURSOR_REGION_ID, stokes_changed);

        return GetSpectralProfileValues<float>(spectral_profile);
    }

    static std::vector<float> Fits4DSpectralProfile(int default_channel, std::string stokes_config_z, bool cube_image_cache) {
        auto path_string = GeneratedFitsImagePath("10 10 10 4", IMAGE_OPTS);

        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        int reserved_memory = cube_image_cache ? std::ceil(10 * 10 * 10 * 4 * sizeof(float) / ONE_MILLION) : 0;
        std::unique_ptr<Frame> frame(new Frame(0, loader, "0", default_channel, reserved_memory));

        int x(4), y(6);
        int channel(5);
        int stokes(0);

        std::string msg;
        frame->SetCursor(x, y);
        frame->SetImageChannels(channel, stokes, msg);

        // Set spectral configs for the cursor
        std::vector<CARTA::SetSpectralRequirements_SpectralConfig> spectral_configs{Message::SpectralConfig(stokes_config_z)};
        frame->SetSpectralRequirements(CURSOR_REGION_ID, spectral_configs);

        // Get cursor spectral profile data from the Frame
        CARTA::SpectralProfile spectral_profile;
        bool stokes_changed = (stokes_config_z == "z");

        frame->FillSpectralProfileData(
            [&](CARTA::SpectralProfileData tmp_spectral_profile) {
                if (tmp_spectral_profile.progress() >= 1.0) {
                    spectral_profile = tmp_spectral_profile.profiles(0);
                }
            },
            CURSOR_REGION_ID, stokes_changed);

        return GetSpectralProfileValues<float>(spectral_profile);
    }
};

TEST_F(CubeImageCacheTest, Fits3DSpatialProfile) {
    Fits3DSpatialProfile(0, false);
    Fits3DSpatialProfile(0, true);
    Fits3DSpatialProfile(1, false);
    Fits3DSpatialProfile(1, true);
}

TEST_F(CubeImageCacheTest, Fits4DSpatialProfile) {
    Fits4DSpatialProfile(0, false);
    Fits4DSpatialProfile(0, true);
    Fits4DSpatialProfile(1, false);
    Fits4DSpatialProfile(1, true);
}

TEST_F(CubeImageCacheTest, Fits3DSpectralProfile) {
    // Check the consistency of two ways (cache or not cache the whole cube image)
    auto spectral_profile1 = Fits3DSpectralProfile(0, true);
    auto spectral_profile2 = Fits3DSpectralProfile(0, false);
    CmpVectors(spectral_profile1, spectral_profile2);
}

TEST_F(CubeImageCacheTest, Fits4DSpectralProfile) {
    // Check the consistency of two ways (cache or not cache the whole cube image)
    auto spectral_profile1 = Fits4DSpectralProfile(0, "Iz", true);
    auto spectral_profile2 = Fits4DSpectralProfile(0, "Iz", false);
    CmpVectors(spectral_profile1, spectral_profile2);

    auto spectral_profile3 = Fits4DSpectralProfile(0, "Qz", true);
    auto spectral_profile4 = Fits4DSpectralProfile(0, "Qz", false);
    CmpVectors(spectral_profile3, spectral_profile4);

    auto spectral_profile5 = Fits4DSpectralProfile(0, "Uz", true);
    auto spectral_profile6 = Fits4DSpectralProfile(0, "Uz", false);
    CmpVectors(spectral_profile5, spectral_profile6);

    auto spectral_profile7 = Fits4DSpectralProfile(0, "Uz", true);
    auto spectral_profile8 = Fits4DSpectralProfile(0, "Uz", false);
    CmpVectors(spectral_profile7, spectral_profile8);

    auto spectral_profile9 = Fits4DSpectralProfile(0, "Ptotalz", true);
    auto spectral_profile10 = Fits4DSpectralProfile(0, "Ptotalz", false);
    CmpVectors(spectral_profile9, spectral_profile10);

    auto spectral_profile11 = Fits4DSpectralProfile(0, "PFtotalz", true);
    auto spectral_profile12 = Fits4DSpectralProfile(0, "PFtotalz", false);
    CmpVectors(spectral_profile11, spectral_profile12);

    auto spectral_profile13 = Fits4DSpectralProfile(0, "Plinearz", true);
    auto spectral_profile14 = Fits4DSpectralProfile(0, "Plinearz", false);
    CmpVectors(spectral_profile13, spectral_profile14);

    auto spectral_profile15 = Fits4DSpectralProfile(0, "PFlinearz", true);
    auto spectral_profile16 = Fits4DSpectralProfile(0, "PFlinearz", false);
    CmpVectors(spectral_profile15, spectral_profile16);

    auto spectral_profile17 = Fits4DSpectralProfile(0, "Panglez", true);
    auto spectral_profile18 = Fits4DSpectralProfile(0, "Panglez", false);
    CmpVectors(spectral_profile17, spectral_profile18);
}
