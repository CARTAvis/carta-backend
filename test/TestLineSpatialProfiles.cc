/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include "CommonTestUtilities.h"
#include "ImageData/FileLoader.h"
#include "Region/Region.h"
#include "Region/RegionHandler.h"
#include "src/Frame/Frame.h"

using namespace carta;

using ::testing::FloatNear;
using ::testing::Pointwise;

class LineSpatialProfileTest : public ::testing::Test {
public:
    static bool SetLineRegion(carta::RegionHandler& region_handler, int file_id, int& region_id, const std::vector<float>& endpoints,
        casacore::CoordinateSystem* csys) {
        std::vector<CARTA::Point> control_points;
        for (auto i = 0; i < endpoints.size(); i += 2) {
            CARTA::Point point;
            point.set_x(endpoints[i]);
            point.set_y(endpoints[i + 1]);
            control_points.push_back(point);
        }

        // Define RegionState for line region and set region (region_id updated)
        CARTA::RegionType region_type = (control_points.size() == 2 ? CARTA::RegionType::LINE : CARTA::RegionType::POLYLINE);
        RegionState region_state(file_id, region_type, control_points, 0.0);
        return region_handler.SetRegion(region_id, region_state, csys);
    }

    static bool GetLineProfiles(const std::string& image_path, const std::vector<float>& endpoints,
        const std::vector<CARTA::SetSpatialRequirements_SpatialConfig>& spatial_reqs,
        std::vector<CARTA::SpatialProfileData>& spatial_profiles) {
        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(image_path));
        std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));
        carta::RegionHandler region_handler;

        // Set line region
        int file_id(0), region_id(-1);
        casacore::CoordinateSystem* csys(frame->CoordinateSystem());
        if (!SetLineRegion(region_handler, file_id, region_id, endpoints, csys)) {
            return false;
        }

        // Get spatial profiles
        if (!region_handler.SetSpatialRequirements(region_id, file_id, frame, spatial_reqs)) {
            return false;
        }

        return region_handler.FillSpatialProfileData(file_id, region_id, spatial_profiles);
    }

    static std::vector<float> ProfileValues(CARTA::SpatialProfile& profile) {
        std::string buffer = profile.raw_values_fp32();
        std::vector<float> values(buffer.size() / sizeof(float));
        memcpy(values.data(), buffer.data(), buffer.size());
        return values;
    }

    void SetUp() {
        setenv("HDF5_USE_FILE_LOCKING", "FALSE", 0);
    }
};

TEST_F(LineSpatialProfileTest, FitsLineProfile) {
    std::string image_path = FileFinder::FitsImagePath("noise_3d.fits");
    std::vector<float> endpoints = {0.0, 0.0, 9.0, 9.0};
    int start(0), end(0), mip(0), width(3);
    std::vector<CARTA::SetSpatialRequirements_SpatialConfig> spatial_reqs = {Message::SpatialConfig("x", start, end, mip, width)};

    std::vector<CARTA::SpatialProfileData> spatial_profiles;
    bool ok = GetLineProfiles(image_path, endpoints, spatial_reqs, spatial_profiles);

    ASSERT_TRUE(ok);
    ASSERT_EQ(spatial_profiles.size(), 1);
    ASSERT_EQ(spatial_profiles[0].profiles_size(), 1);
}

TEST_F(LineSpatialProfileTest, Hdf5LineProfile) {
    std::string image_path = FileFinder::Hdf5ImagePath("noise_10px_10px.hdf5");
    std::vector<float> endpoints = {0.0, 0.0, 9.0, 9.0};
    int start(0), end(0), mip(0), width(3);
    std::vector<CARTA::SetSpatialRequirements_SpatialConfig> spatial_reqs = {Message::SpatialConfig("x", start, end, mip, width)};

    std::vector<CARTA::SpatialProfileData> spatial_profiles;
    bool ok = GetLineProfiles(image_path, endpoints, spatial_reqs, spatial_profiles);

    ASSERT_TRUE(ok);
    ASSERT_EQ(spatial_profiles.size(), 1);
    ASSERT_EQ(spatial_profiles[0].profiles_size(), 1);
}

TEST_F(LineSpatialProfileTest, FitsHorizontalCutProfile) {
    std::string image_path = FileFinder::FitsImagePath("noise_3d.fits");
    std::vector<float> endpoints = {9.0, 5.0, 1.0, 5.0}; // Set line region at y=5
    int start(0), end(0), mip(0), width(1);
    std::vector<CARTA::SetSpatialRequirements_SpatialConfig> spatial_reqs = {Message::SpatialConfig("x", start, end, mip, width)};

    std::vector<CARTA::SpatialProfileData> spatial_profiles;
    bool ok = GetLineProfiles(image_path, endpoints, spatial_reqs, spatial_profiles);

    ASSERT_TRUE(ok);
    ASSERT_EQ(spatial_profiles.size(), 1);
    ASSERT_EQ(spatial_profiles[0].profiles_size(), 1);

    // Profile data
    auto profile = spatial_profiles[0].profiles(0);
    std::vector<float> profile_data = ProfileValues(profile);
    EXPECT_EQ(profile_data.size(), 9);

    // Read image data slice for first channel
    FitsDataReader reader(image_path);
    auto image_data = reader.ReadRegion({1, 5, 0}, {10, 6, 1});

    // Profile data width=1 of horizontal line is same as slice
    CmpVectors(profile_data, image_data);
}

TEST_F(LineSpatialProfileTest, FitsVerticalCutProfile) {
    std::string image_path = FileFinder::FitsImagePath("noise_3d.fits");
    std::vector<float> endpoints = {5.0, 9.0, 5.0, 1.0}; // Set line region at x=5
    int start(0), end(0), mip(0), width(1);
    std::vector<CARTA::SetSpatialRequirements_SpatialConfig> spatial_reqs = {Message::SpatialConfig("y", start, end, mip, width)};

    std::vector<CARTA::SpatialProfileData> spatial_profiles;
    bool ok = GetLineProfiles(image_path, endpoints, spatial_reqs, spatial_profiles);

    ASSERT_TRUE(ok);
    ASSERT_EQ(spatial_profiles.size(), 1);
    ASSERT_EQ(spatial_profiles[0].profiles_size(), 1);

    // Profile data
    auto profile = spatial_profiles[0].profiles(0);
    std::vector<float> profile_data = ProfileValues(profile);
    EXPECT_EQ(profile_data.size(), 9);

    // Read image data slice for first channel
    FitsDataReader reader(image_path);
    auto image_data = reader.ReadRegion({5, 1, 0}, {6, 10, 1});

    // Profile data width=1 of horizontal line is same as slice
    CmpVectors(profile_data, image_data);
}

TEST_F(LineSpatialProfileTest, FitsPolylineProfile) {
    std::string image_path = FileFinder::FitsImagePath("noise_3d.fits");
    std::vector<float> endpoints = {9.0, 5.0, 9.0, 1.0, 1.0, 1.0};
    int start(0), end(0), mip(0), width(1);
    std::vector<CARTA::SetSpatialRequirements_SpatialConfig> spatial_reqs = {Message::SpatialConfig("x", start, end, mip, width)};
    std::vector<CARTA::SpatialProfileData> spatial_profiles;

    bool ok = GetLineProfiles(image_path, endpoints, spatial_reqs, spatial_profiles);
    ASSERT_TRUE(ok);
    ASSERT_EQ(spatial_profiles.size(), 1);
    ASSERT_EQ(spatial_profiles[0].profiles_size(), 1);

    // Profile data
    auto profile = spatial_profiles[0].profiles(0);
    std::vector<float> profile_data = ProfileValues(profile);
    EXPECT_EQ(profile_data.size(), 13);

    // Read image data slice for first channel; from end (line 1 end to start) to beginning (line 0 end to start
    FitsDataReader reader(image_path);
    auto line1_data = reader.ReadRegion({1, 1, 0}, {10, 2, 1});
    // Trim line 0: [9, 1] already covered by line 1
    auto line0_data = reader.ReadRegion({9, 2, 0}, {10, 6, 1});
    auto image_data = line1_data;
    for (size_t i = 0; i < line0_data.size(); ++i) {
        image_data.push_back(line0_data[i]);
    }

    // Profile data width=1 of polyline is same as slices
    CmpVectors(profile_data, image_data);
}
