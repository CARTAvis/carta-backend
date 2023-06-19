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

class CubeImageCacheTest : public ::testing::Test, public ImageGenerator {
public:
    static std::tuple<CARTA::SpatialProfile, CARTA::SpatialProfile> GetProfiles(CARTA::SpatialProfileData& data) {
        if (data.profiles(0).coordinate().back() == 'x') {
            return {data.profiles(0), data.profiles(1)};
        }
        return {data.profiles(1), data.profiles(0)};
    }

    static void Fits3DImage(int default_channel, bool cube_image_cache) {
        auto path_string = GeneratedFitsImagePath("10 10 10", IMAGE_OPTS);
        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        std::unique_ptr<Frame> frame(new Frame(0, loader, "0", default_channel, cube_image_cache));
        FitsDataReader reader(path_string);

        int x(4), y(6);
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

    static void Fits4DImage(int default_channel, bool cube_image_cache) {
        auto path_string = GeneratedFitsImagePath("10 10 10 4", IMAGE_OPTS);
        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
        std::unique_ptr<Frame> frame(new Frame(0, loader, "0", default_channel, cube_image_cache));
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
};

TEST_F(CubeImageCacheTest, Fits3DImage) {
    Fits3DImage(0, false);
    Fits3DImage(0, true);
    Fits3DImage(1, false);
    Fits3DImage(1, true);
}

TEST_F(CubeImageCacheTest, Fits4DImage) {
    Fits4DImage(0, false);
    Fits4DImage(0, true);
    Fits4DImage(1, false);
    Fits4DImage(1, true);
}
