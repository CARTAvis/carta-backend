/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include "Frame.h"
#include "ImageData/FileLoader.h"

#include "CommonTestUtilities.h"

using namespace carta;

using ::testing::FloatNear;
using ::testing::Pointwise;

class SpatialProfileTest : public ::testing::Test, public ImageGenerator {
public:
    static std::tuple<CARTA::SpatialProfile, CARTA::SpatialProfile> GetProfiles(CARTA::SpatialProfileData& data) {
        if (data.profiles(0).coordinate() == "x") {
            return {data.profiles(0), data.profiles(1)};
        } else {
            return {data.profiles(1), data.profiles(0)};
        }
    }

    static std::vector<float> ProfileValues(CARTA::SpatialProfile& profile) {
        std::string buffer = profile.raw_values_fp32();
        std::vector<float> values(buffer.size() / sizeof(float));
        memcpy(values.data(), buffer.data(), buffer.size());
        return values;
    }

    static CARTA::SetSpatialRequirements_SpatialConfig SpatialConfig(std::string coordinate, int start = 0, int end = 0, int mip = 0) {
        CARTA::SetSpatialRequirements_SpatialConfig conf;
        conf.set_coordinate(coordinate);
        conf.set_start(start);
        conf.set_end(end);
        conf.set_mip(mip);
        return conf;
    }

    static std::vector<float> Decimated(std::vector<float> full_resolution, int mip) {
        size_t num_decimated_pairs = std::ceil((float)full_resolution.size() / (mip * 2));
        std::vector<float> result(num_decimated_pairs * 2);
        for (size_t i = 0; i < num_decimated_pairs; i++) {
            std::vector<float> segment(
                full_resolution.begin() + i * mip * 2, std::min(full_resolution.begin() + (i + 1) * mip * 2, full_resolution.end()));
            // First occurrence of smallest element
            auto minpos = std::min_element(segment.begin(), segment.end());
            // Last occurrence of largest element (because the real code uses min_max_element)
            auto maxpos = (std::max_element(segment.rbegin(), segment.rend()) + 1).base();
            result[i * 2] = (minpos < maxpos) ? *minpos : *maxpos;
            result[i * 2 + 1] = (minpos < maxpos) ? *maxpos : *minpos;
        }
        return result;
    }

    void SetUp() {
        setenv("HDF5_USE_FILE_LOCKING", "FALSE", 0);
    }
};

TEST_F(SpatialProfileTest, SubTileFitsImageProfile) {
    auto path_string = GeneratedFitsImagePath("10 10");
    std::unique_ptr<Frame> frame(new Frame(0, carta::FileLoader::GetLoader(path_string), "0"));
    FitsDataReader reader(path_string);

    std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {SpatialConfig("x"), SpatialConfig("y")};
    frame->SetSpatialRequirements(CURSOR_REGION_ID, profiles);
    frame->SetCursor(5, 5);

    CARTA::SpatialProfileData data;
    frame->FillSpatialProfileData(CURSOR_REGION_ID, data);

    EXPECT_EQ(data.file_id(), 0);
    EXPECT_EQ(data.region_id(), CURSOR_REGION_ID);
    EXPECT_EQ(data.x(), 5);
    EXPECT_EQ(data.y(), 5);
    EXPECT_EQ(data.channel(), 0);
    EXPECT_EQ(data.stokes(), 0);
    EXPECT_FLOAT_EQ(data.value(), reader.ReadPointXY(5, 5));
    EXPECT_EQ(data.profiles_size(), 2);

    auto [x_profile, y_profile] = GetProfiles(data);

    EXPECT_EQ(x_profile.start(), 0);
    EXPECT_EQ(x_profile.end(), 10);
    EXPECT_EQ(x_profile.mip(), 0);
    auto x_vals = ProfileValues(x_profile);
    EXPECT_EQ(x_vals.size(), 10);
    EXPECT_THAT(x_vals, Pointwise(FloatNear(1e-5), reader.ReadProfileX(5)));

    EXPECT_EQ(y_profile.start(), 0);
    EXPECT_EQ(y_profile.end(), 10);
    EXPECT_EQ(y_profile.mip(), 0);
    auto y_vals = ProfileValues(y_profile);
    EXPECT_EQ(y_vals.size(), 10);
    EXPECT_THAT(y_vals, Pointwise(FloatNear(1e-5), reader.ReadProfileY(5)));
}

TEST_F(SpatialProfileTest, SubTileHdf5ImageProfile) {
    auto path_string = GeneratedHdf5ImagePath("10 10");
    std::unique_ptr<Frame> frame(new Frame(0, carta::FileLoader::GetLoader(path_string), "0"));
    Hdf5DataReader reader(path_string);

    std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {SpatialConfig("x"), SpatialConfig("y")};
    frame->SetSpatialRequirements(CURSOR_REGION_ID, profiles);
    frame->SetCursor(5, 5);

    CARTA::SpatialProfileData data;
    frame->FillSpatialProfileData(CURSOR_REGION_ID, data);

    EXPECT_EQ(data.file_id(), 0);
    EXPECT_EQ(data.region_id(), CURSOR_REGION_ID);
    EXPECT_EQ(data.x(), 5);
    EXPECT_EQ(data.y(), 5);
    EXPECT_EQ(data.channel(), 0);
    EXPECT_EQ(data.stokes(), 0);
    EXPECT_FLOAT_EQ(data.value(), reader.ReadPointXY(5, 5));
    EXPECT_EQ(data.profiles_size(), 2);

    auto [x_profile, y_profile] = GetProfiles(data);

    EXPECT_EQ(x_profile.start(), 0);
    EXPECT_EQ(x_profile.end(), 10);
    EXPECT_EQ(x_profile.mip(), 0);
    auto x_vals = ProfileValues(x_profile);
    EXPECT_EQ(x_vals.size(), 10);
    EXPECT_THAT(x_vals, Pointwise(FloatNear(1e-5), reader.ReadProfileX(5)));

    EXPECT_EQ(y_profile.start(), 0);
    EXPECT_EQ(y_profile.end(), 10);
    EXPECT_EQ(y_profile.mip(), 0);
    auto y_vals = ProfileValues(y_profile);
    EXPECT_EQ(y_vals.size(), 10);
    EXPECT_THAT(y_vals, Pointwise(FloatNear(1e-5), reader.ReadProfileY(5)));
}

TEST_F(SpatialProfileTest, LowResFitsProfile) {
    auto path_string = GeneratedFitsImagePath("100 100");
    std::unique_ptr<Frame> frame(new Frame(0, carta::FileLoader::GetLoader(path_string), "0"));
    FitsDataReader reader(path_string);

    std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {SpatialConfig("x", 0, 0, 4), SpatialConfig("y", 0, 0, 4)};
    frame->SetSpatialRequirements(CURSOR_REGION_ID, profiles);
    frame->SetCursor(50, 50);

    CARTA::SpatialProfileData data;
    frame->FillSpatialProfileData(CURSOR_REGION_ID, data);

    EXPECT_EQ(data.profiles_size(), 2);

    auto [x_profile, y_profile] = GetProfiles(data);

    EXPECT_EQ(x_profile.start(), 0);
    EXPECT_EQ(x_profile.end(), 100);
    EXPECT_EQ(x_profile.mip(), 4);
    auto x_vals = ProfileValues(x_profile);
    EXPECT_EQ(x_vals.size(), 26);
    EXPECT_THAT(x_vals, Pointwise(FloatNear(1e-5), Decimated(reader.ReadProfileX(50), 4)));

    EXPECT_EQ(y_profile.start(), 0);
    EXPECT_EQ(y_profile.end(), 100);
    EXPECT_EQ(y_profile.mip(), 4);
    auto y_vals = ProfileValues(y_profile);
    EXPECT_EQ(y_vals.size(), 26);
    EXPECT_THAT(y_vals, Pointwise(FloatNear(1e-5), Decimated(reader.ReadProfileY(50), 4)));
}
