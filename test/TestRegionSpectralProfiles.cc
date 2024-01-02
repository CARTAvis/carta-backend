/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
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

class RegionSpectralProfileTest : public ::testing::Test {
public:
    static bool SetRegion(carta::RegionHandler& region_handler, int file_id, int& region_id, const std::vector<float>& points,
        std::shared_ptr<casacore::CoordinateSystem> csys, bool is_annotation) {
        std::vector<CARTA::Point> control_points;
        for (auto i = 0; i < points.size(); i += 2) {
            control_points.push_back(Message::Point(points[i], points[i + 1]));
        }

        // Define RegionState for line region and set region (region_id updated)
        auto npoints(control_points.size());
        CARTA::RegionType region_type = CARTA::RegionType::POLYGON;
        if (is_annotation) {
            region_type = CARTA::RegionType::ANNPOLYGON;
        }
        RegionState region_state(file_id, region_type, control_points, 0.0);
        return region_handler.SetRegion(region_id, region_state, csys);
    }

    static bool SpectralProfile(const std::string& image_path, const std::vector<float>& endpoints, CARTA::SpectralProfileData& spectral_data,
        bool is_annotation = false) {
        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(image_path));
        std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));
        carta::RegionHandler region_handler;

        // Set polygon region
        int file_id(0), region_id(-1);
        auto csys = frame->CoordinateSystem();
        if (!SetRegion(region_handler, file_id, region_id, endpoints, csys, is_annotation)) {
            return false;
        }

        // Set spectral requirements (Message requests 10 stats types)
        auto spectral_req_message = Message::SetSpectralRequirements(file_id, region_id, "z");
        std::vector<CARTA::SetSpectralRequirements_SpectralConfig> spectral_requirements = {
            spectral_req_message.spectral_profiles().begin(), spectral_req_message.spectral_profiles().end()};
        if (!region_handler.SetSpectralRequirements(region_id, file_id, frame, spectral_requirements)) {
            return false;
        }

        // Get spectral profile
        return region_handler.FillSpectralProfileData(
            [&](CARTA::SpectralProfileData profile_data) { spectral_data = profile_data; }, region_id, file_id, false);
    }

    static std::vector<double> GetExpectedMeanProfile(std::string& image_path, int num_channels) {
        // Read image for profile for box region blc (0,0) trc (3,3)
        FitsDataReader reader(image_path);
        std::vector<double> profile;
        for (hsize_t i = 0; i < num_channels; ++i) {
            auto channel_data = reader.ReadRegion({0, 0, i}, {4, 4, i + 1});
            double sum = std::accumulate(channel_data.begin(), channel_data.end(), 0.0);
            double mean = sum / channel_data.size();
            profile.push_back(mean);
        }
        return profile;
    }
};

TEST_F(RegionSpectralProfileTest, TestFitsSpectralProfile) {
    std::string image_path = FileFinder::FitsImagePath("noise_3d.fits");
    int num_channels = 10;
    std::vector<float> endpoints = {0.0, 0.0, 0.0, 3.0, 3.0, 3.0, 3.0, 0.0};
    CARTA::SpectralProfileData spectral_data;
    bool ok = SpectralProfile(image_path, endpoints, spectral_data);

    // Check spectral profiles
    ASSERT_TRUE(ok);
    ASSERT_EQ(spectral_data.file_id(), 0);
    ASSERT_EQ(spectral_data.region_id(), 1);
    ASSERT_EQ(spectral_data.stokes(), 0);
    ASSERT_EQ(spectral_data.progress(), 1.0);

    auto num_profiles = spectral_data.profiles_size();
    // Expected types set in Message::SetSpectralRequirements
    std::vector<CARTA::StatsType> expected_types = {CARTA::StatsType::NumPixels, CARTA::StatsType::Sum, CARTA::StatsType::FluxDensity,
        CARTA::StatsType::Mean, CARTA::StatsType::RMS, CARTA::StatsType::Sigma, CARTA::StatsType::SumSq, CARTA::StatsType::Min,
        CARTA::StatsType::Max, CARTA::StatsType::Extrema};
    ASSERT_EQ(num_profiles, expected_types.size());


    for (int i = 0; i < num_profiles; ++i) {
        auto profile = spectral_data.profiles(i);
        ASSERT_EQ(profile.coordinate(), "z");
        ASSERT_EQ(profile.stats_type(), expected_types[i]);
        auto values = profile.raw_values_fp64(); // string (bytes)
        ASSERT_EQ(values.size(), num_channels * 8); // double (8-byte) values

        if (profile.stats_type() == CARTA::StatsType::Mean) {
			std::vector<double> expected_profile = GetExpectedMeanProfile(image_path, num_channels);
			std::vector<double> actual_profile = GetSpectralProfileValues<double>(profile);
            CmpVectors<double>(actual_profile, expected_profile);
        }
    }
}

TEST_F(RegionSpectralProfileTest, TestFitsAnnotationRegionSpectralProfile) {
    std::string image_path = FileFinder::FitsImagePath("noise_3d.fits");
    std::vector<float> endpoints = {0.0, 0.0, 0.0, 3.0, 3.0, 3.0, 3.0, 0.0};
    CARTA::SpectralProfileData spectral_data;
    bool ok = SpectralProfile(image_path, endpoints, spectral_data, true);
    ASSERT_FALSE(ok);
}
