/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
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

class RegionSpatialProfileTest : public ::testing::Test {
public:
    static CARTA::SetSpatialRequirements_SpatialConfig SpatialConfig(
        std::string coordinate, int32_t start = 0, int32_t end = 0, int32_t mip = 0, int32_t width = 0) {
        CARTA::SetSpatialRequirements_SpatialConfig spatial_config;
        spatial_config.set_coordinate(coordinate);
        spatial_config.set_start(start);
        spatial_config.set_end(end);
        spatial_config.set_mip(mip);
        spatial_config.set_width(width);
        return spatial_config;
    }

    static bool SetRegion(carta::RegionHandler& region_handler, int file_id, int& region_id, const std::vector<float>& points,
        std::shared_ptr<casacore::CoordinateSystem> csys, bool is_annotation) {
        std::vector<CARTA::Point> control_points;
        for (auto i = 0; i < points.size(); i += 2) {
            control_points.push_back(Message::Point(points[i], points[i + 1]));
        }

        // Define RegionState for line region and set region (region_id updated)
        auto npoints(control_points.size());
        CARTA::RegionType region_type;
        if (npoints == 1) {
            if (is_annotation) {
                region_type = CARTA::RegionType::ANNPOINT;
            } else {
                region_type = CARTA::RegionType::POINT;
            }
        } else {
            region_type = CARTA::RegionType::LINE;
            if (is_annotation) {
                if (npoints > 2) {
                    region_type = CARTA::RegionType::ANNPOLYLINE;
                } else {
                    region_type = CARTA::RegionType::ANNLINE;
                }
            } else {
                if (npoints > 2) {
                    region_type = CARTA::RegionType::POLYLINE;
                } else {
                    region_type = CARTA::RegionType::LINE;
                }
            }
        }
        RegionState region_state(file_id, region_type, control_points, 0.0);
        return region_handler.SetRegion(region_id, region_state, csys);
    }

    static bool SetRectangleRegion(carta::RegionHandler& region_handler, int file_id, int& region_id, const std::vector<float>& points,
        std::shared_ptr<casacore::CoordinateSystem> csys) {
        std::vector<CARTA::Point> control_points;
        for (auto i = 0; i < points.size(); i += 2) {
            control_points.push_back(Message::Point(points[i], points[i + 1]));
        }
        CARTA::RegionType region_type(CARTA::RECTANGLE);
        RegionState region_state(file_id, region_type, control_points, 0.0);
        return region_handler.SetRegion(region_id, region_state, csys);
    }

    static bool RegionSpatialProfile(const std::string& image_path, const std::vector<float>& endpoints,
        const std::vector<CARTA::SetSpatialRequirements_SpatialConfig>& spatial_reqs, CARTA::SpatialProfileData& spatial_profile_message,
        bool is_annotation = false) {
        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(image_path));
        std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));
        carta::RegionHandler region_handler;

        // Set line region
        int file_id(0), region_id(-1);
        auto csys = frame->CoordinateSystem();

        if (!SetRegion(region_handler, file_id, region_id, endpoints, csys, is_annotation)) {
            return false;
        }

        // Set spatial requirements
        if (!region_handler.SetSpatialRequirements(region_id, file_id, frame, spatial_reqs)) {
            return false;
        }

        // Get spatial profiles
        return region_handler.FillSpatialProfileData(
            [&](CARTA::SpatialProfileData profile_message) { spatial_profile_message = profile_message; }, file_id, region_id);
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

    static void TestAveragingWidthRange(int width, bool expected_width_range) {
        std::string image_path = FileFinder::FitsImagePath("noise_3d.fits");
        int file_id(0), region_id(-1);
        std::vector<float> endpoints = {0.0, 0.0, 9.0, 9.0};
        int start(0), end(0), mip(0);
        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> spatial_reqs = {
            RegionSpatialProfileTest::SpatialConfig("x", start, end, mip, width)};
        CARTA::SpatialProfileData spatial_profile_message;

        if (expected_width_range) {
            ASSERT_TRUE(RegionSpatialProfile(image_path, endpoints, spatial_reqs, spatial_profile_message));
            ASSERT_EQ(spatial_profile_message.profiles_size(), 1);
        } else {
            ASSERT_FALSE(RegionSpatialProfile(image_path, endpoints, spatial_reqs, spatial_profile_message));
            ASSERT_EQ(spatial_profile_message.profiles_size(), 0);
        }
    }
};

TEST_F(RegionSpatialProfileTest, TestSpatialRequirements) {
    std::string image_path = FileFinder::FitsImagePath("noise_3d.fits");
    std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(image_path));
    std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));

    // Set line region
    carta::RegionHandler region_handler;
    int file_id(0), region_id(-1);
    std::vector<float> endpoints = {0.0, 0.0, 9.0, 9.0};
    auto csys = frame->CoordinateSystem();
    bool is_annotation(false);
    bool ok = SetRegion(region_handler, file_id, region_id, endpoints, csys, is_annotation);
    ASSERT_TRUE(ok);

    // Set spatial requirements
    int start(0), end(0), mip(0), width(3);
    std::vector<CARTA::SetSpatialRequirements_SpatialConfig> spatial_reqs = {
        RegionSpatialProfileTest::SpatialConfig("x", start, end, mip, width)};
    ok = region_handler.SetSpatialRequirements(region_id, file_id, frame, spatial_reqs);
    ASSERT_TRUE(ok);

    // Set rectangle region
    region_id = -1;
    endpoints = {5.0, 5.0, 3.0, 2.0};
    ok = SetRectangleRegion(region_handler, file_id, region_id, endpoints, csys);
    ASSERT_TRUE(ok);

    // Set spatial requirements for invalid region type
    ok = region_handler.SetSpatialRequirements(region_id, file_id, frame, spatial_reqs);
    ASSERT_FALSE(ok);
}

TEST_F(RegionSpatialProfileTest, FitsLineProfile) {
    std::string image_path = FileFinder::FitsImagePath("noise_3d.fits");
    std::vector<float> endpoints = {0.0, 0.0, 9.0, 9.0};
    int start(0), end(0), mip(0), width(3);
    std::vector<CARTA::SetSpatialRequirements_SpatialConfig> spatial_reqs = {
        RegionSpatialProfileTest::SpatialConfig("x", start, end, mip, width)};

    CARTA::SpatialProfileData spatial_profile_message;
    bool ok = RegionSpatialProfile(image_path, endpoints, spatial_reqs, spatial_profile_message);

    ASSERT_TRUE(ok);
    ASSERT_EQ(spatial_profile_message.profiles_size(), 1);
}

TEST_F(RegionSpatialProfileTest, Hdf5LineProfile) {
    std::string image_path = FileFinder::Hdf5ImagePath("noise_10px_10px.hdf5");
    std::vector<float> endpoints = {0.0, 0.0, 9.0, 9.0};
    int start(0), end(0), mip(0), width(3);
    std::vector<CARTA::SetSpatialRequirements_SpatialConfig> spatial_reqs = {
        RegionSpatialProfileTest::SpatialConfig("x", start, end, mip, width)};

    CARTA::SpatialProfileData spatial_profile_message;
    bool ok = RegionSpatialProfile(image_path, endpoints, spatial_reqs, spatial_profile_message);

    ASSERT_TRUE(ok);
    ASSERT_EQ(spatial_profile_message.profiles_size(), 1);
}

TEST_F(RegionSpatialProfileTest, FitsHorizontalCutProfile) {
    std::string image_path = FileFinder::FitsImagePath("noise_3d.fits");
    std::vector<float> endpoints = {9.0, 5.0, 1.0, 5.0}; // Set line region at y=5
    int start(0), end(0), mip(0), width(1);
    std::vector<CARTA::SetSpatialRequirements_SpatialConfig> spatial_reqs = {
        RegionSpatialProfileTest::SpatialConfig("x", start, end, mip, width)};

    CARTA::SpatialProfileData spatial_profile_message;
    bool ok = RegionSpatialProfile(image_path, endpoints, spatial_reqs, spatial_profile_message);

    ASSERT_TRUE(ok);
    ASSERT_EQ(spatial_profile_message.profiles_size(), 1);

    // Profile data
    auto profile = spatial_profile_message.profiles(0);
    std::vector<float> profile_data = ProfileValues(profile);
    EXPECT_EQ(profile_data.size(), 9);

    // Read image data slice for first channel
    // Profile data of horizontal line with width=1 is same as slice
    FitsDataReader reader(image_path);
    auto image_data = reader.ReadRegion({1, 5, 0}, {10, 6, 1});
    CmpVectors<float>(profile_data, image_data);
}

TEST_F(RegionSpatialProfileTest, FitsVerticalCutProfile) {
    std::string image_path = FileFinder::FitsImagePath("noise_3d.fits");
    std::vector<float> endpoints = {5.0, 9.0, 5.0, 1.0}; // Set line region at x=5
    int start(0), end(0), mip(0), width(1);
    std::vector<CARTA::SetSpatialRequirements_SpatialConfig> spatial_reqs = {
        RegionSpatialProfileTest::SpatialConfig("y", start, end, mip, width)};

    CARTA::SpatialProfileData spatial_profile_message;
    bool ok = RegionSpatialProfile(image_path, endpoints, spatial_reqs, spatial_profile_message);

    ASSERT_TRUE(ok);
    ASSERT_EQ(spatial_profile_message.profiles_size(), 1);

    // Profile data
    auto profile = spatial_profile_message.profiles(0);
    std::vector<float> profile_data = ProfileValues(profile);
    EXPECT_EQ(profile_data.size(), 9);

    // Read image data slice for first channel
    // Profile data of vertical line with width=1 is same as slice
    FitsDataReader reader(image_path);
    auto image_data = reader.ReadRegion({5, 1, 0}, {6, 10, 1});
    CmpVectors<float>(profile_data, image_data);
}

TEST_F(RegionSpatialProfileTest, FitsPolylineProfile) {
    std::string image_path = FileFinder::FitsImagePath("noise_3d.fits");
    std::vector<float> endpoints = {1.0, 1.0, 9.0, 1.0, 9.0, 5.0};
    int start(0), end(0), mip(0), width(1);
    std::vector<CARTA::SetSpatialRequirements_SpatialConfig> spatial_reqs = {
        RegionSpatialProfileTest::SpatialConfig("x", start, end, mip, width)};

    CARTA::SpatialProfileData spatial_profile_message;
    bool ok = RegionSpatialProfile(image_path, endpoints, spatial_reqs, spatial_profile_message);
    ASSERT_TRUE(ok);
    ASSERT_EQ(spatial_profile_message.profiles_size(), 1);

    // Profile data
    auto profile = spatial_profile_message.profiles(0);
    std::vector<float> profile_data = ProfileValues(profile);
    EXPECT_EQ(profile_data.size(), 13);

    // Read image data slice for first channel
    FitsDataReader reader(image_path);
    auto line0_data = reader.ReadRegion({1, 1, 0}, {10, 2, 1});
    // Trim line 1: [9, 1] already covered by line 0
    auto line1_data = reader.ReadRegion({9, 2, 0}, {10, 6, 1});
    auto image_data = line0_data;
    for (size_t i = 0; i < line1_data.size(); ++i) {
        image_data.push_back(line1_data[i]);
    }

    // Profile data of polyline with width=1 is same as image data slices
    CmpVectors<float>(profile_data, image_data);
}

TEST_F(RegionSpatialProfileTest, AveragingWidthRange) {
    TestAveragingWidthRange(0, false);
    TestAveragingWidthRange(1, true);
    TestAveragingWidthRange(20, true);
    TestAveragingWidthRange(21, false);
}

TEST_F(RegionSpatialProfileTest, FitsAnnotationLineProfile) {
    std::string image_path = FileFinder::FitsImagePath("noise_3d.fits");
    std::vector<float> endpoints = {0.0, 0.0, 9.0, 9.0};
    int start(0), end(0), mip(0), width(3);
    std::vector<CARTA::SetSpatialRequirements_SpatialConfig> spatial_reqs = {
        RegionSpatialProfileTest::SpatialConfig("x", start, end, mip, width)};

    CARTA::SpatialProfileData spatial_profile_message;
    bool is_annotation(true);
    bool ok = RegionSpatialProfile(image_path, endpoints, spatial_reqs, spatial_profile_message, is_annotation);
    ASSERT_FALSE(ok);
}

TEST_F(RegionSpatialProfileTest, FitsPointProfile) {
    std::string image_path = FileFinder::FitsImagePath("noise_3d.fits");
    std::vector<float> endpoints = {0.0, 0.0};
    int start(0), end(0), mip(0), width(1);
    std::vector<CARTA::SetSpatialRequirements_SpatialConfig> spatial_reqs = {
        RegionSpatialProfileTest::SpatialConfig("x", start, end, mip, width),
        RegionSpatialProfileTest::SpatialConfig("y", start, end, mip, width)};

    CARTA::SpatialProfileData spatial_profile_message;
    bool ok = RegionSpatialProfile(image_path, endpoints, spatial_reqs, spatial_profile_message);

    ASSERT_TRUE(ok);
    ASSERT_EQ(spatial_profile_message.profiles_size(), 2);

    // Profile data for 10 x 10 image frame
    auto profile = spatial_profile_message.profiles(0);
    std::vector<float> profile_data = ProfileValues(profile);
    EXPECT_EQ(profile_data.size(), 10);

    // Read image data slice for first channel
    // Profile data of point is same as slice
    FitsDataReader reader(image_path);
    auto image_data = reader.ReadRegion({0, 0, 0}, {10, 1, 1});
    CmpVectors<float>(profile_data, image_data);
}

TEST_F(RegionSpatialProfileTest, Hdf5PointProfile) {
    std::string image_path = FileFinder::Hdf5ImagePath("noise_10px_10px.hdf5");
    std::vector<float> points = {0.0, 0.0};
    int start(0), end(0), mip(0), width(1);
    std::vector<CARTA::SetSpatialRequirements_SpatialConfig> spatial_reqs = {
        RegionSpatialProfileTest::SpatialConfig("x", start, end, mip, width)};

    CARTA::SpatialProfileData spatial_profile_message;
    bool ok = RegionSpatialProfile(image_path, points, spatial_reqs, spatial_profile_message);

    ASSERT_TRUE(ok);
    ASSERT_EQ(spatial_profile_message.profiles_size(), 1);

    // Profile data for 10 x 10 image frame
    auto profile = spatial_profile_message.profiles(0);
    std::vector<float> profile_data = ProfileValues(profile);
    EXPECT_EQ(profile_data.size(), 10);

    // Read image data slice for first channel
    // Profile data of point is same as slice
    Hdf5DataReader reader(image_path);
    auto image_data = reader.ReadRegion({0, 0, 0}, {10, 1, 1});
    CmpVectors<float>(profile_data, image_data);
}

TEST_F(RegionSpatialProfileTest, FitsPointProfileOutsideImage) {
    std::string image_path = FileFinder::FitsImagePath("noise_3d.fits");
    std::vector<float> endpoints = {-2.0, -2.0};
    int start(0), end(0), mip(0), width(1);
    std::vector<CARTA::SetSpatialRequirements_SpatialConfig> spatial_reqs = {
        RegionSpatialProfileTest::SpatialConfig("x", start, end, mip, width)};

    CARTA::SpatialProfileData spatial_profile_message;
    bool ok = RegionSpatialProfile(image_path, endpoints, spatial_reqs, spatial_profile_message);

    ASSERT_FALSE(ok);
}

TEST_F(RegionSpatialProfileTest, FitsAnnotationPointProfile) {
    std::string image_path = FileFinder::FitsImagePath("noise_3d.fits");
    std::vector<float> point = {0.0, 0.0};
    int start(0), end(0), mip(0), width(3);
    std::vector<CARTA::SetSpatialRequirements_SpatialConfig> spatial_reqs = {
        RegionSpatialProfileTest::SpatialConfig("x", start, end, mip, width)};

    CARTA::SpatialProfileData spatial_profile_message;
    bool is_annotation(true);
    bool ok = RegionSpatialProfile(image_path, point, spatial_reqs, spatial_profile_message, is_annotation);

    ASSERT_FALSE(ok);
}

TEST_F(RegionSpatialProfileTest, FitsMovePointProfile) {
    std::string image_path = FileFinder::FitsImagePath("noise_3d.fits");
    std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(image_path));
    std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));

    // Set point region
    carta::RegionHandler region_handler;
    int file_id(0), region_id(-1);
    std::vector<float> endpoints = {0.0, 0.0};
    auto csys = frame->CoordinateSystem();
    bool is_annotation(false);
    bool ok = SetRegion(region_handler, file_id, region_id, endpoints, csys, is_annotation);
    ASSERT_TRUE(ok);

    // Set spatial requirements
    int start(0), end(0), mip(0), width(3);
    std::vector<CARTA::SetSpatialRequirements_SpatialConfig> spatial_reqs = {
        RegionSpatialProfileTest::SpatialConfig("x", start, end, mip, width)};
    ok = region_handler.SetSpatialRequirements(region_id, file_id, frame, spatial_reqs);
    ASSERT_TRUE(ok);

    // Get spatial profile
    CARTA::SpatialProfileData spatial_profile_message;
    ok = region_handler.FillSpatialProfileData(
        [&](CARTA::SpatialProfileData profile_message) { spatial_profile_message = profile_message; }, file_id, region_id);
    ASSERT_TRUE(ok);
    ASSERT_EQ(spatial_profile_message.profiles_size(), 1);

    // Move point
    endpoints = {9.0, 9.0};
    ok = SetRegion(region_handler, file_id, region_id, endpoints, csys, is_annotation);
    ASSERT_TRUE(ok);

    // Get spatial profile
    ok = region_handler.FillSpatialProfileData(
        [&](CARTA::SpatialProfileData profile_message) { spatial_profile_message = profile_message; }, file_id, region_id);
    ASSERT_TRUE(ok);
    ASSERT_EQ(spatial_profile_message.profiles_size(), 1);
}
