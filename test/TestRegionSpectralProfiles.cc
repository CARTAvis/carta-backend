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

class RegionSpectralProfileTest : public ::testing::Test {
public:
    static CARTA::SetSpectralRequirements SetSpectralRequirements(int32_t file_id, int32_t region_id, std::string coordinate) {
        CARTA::SetSpectralRequirements set_spectral_requirements;
        set_spectral_requirements.set_file_id(file_id);
        set_spectral_requirements.set_region_id(region_id);
        auto* spectral_profiles = set_spectral_requirements.add_spectral_profiles();
        spectral_profiles->set_coordinate(coordinate);
        spectral_profiles->add_stats_types(CARTA::StatsType::NumPixels);
        spectral_profiles->add_stats_types(CARTA::StatsType::Sum);
        spectral_profiles->add_stats_types(CARTA::StatsType::FluxDensity);
        spectral_profiles->add_stats_types(CARTA::StatsType::Mean);
        spectral_profiles->add_stats_types(CARTA::StatsType::RMS);
        spectral_profiles->add_stats_types(CARTA::StatsType::Sigma);
        spectral_profiles->add_stats_types(CARTA::StatsType::SumSq);
        spectral_profiles->add_stats_types(CARTA::StatsType::Min);
        spectral_profiles->add_stats_types(CARTA::StatsType::Max);
        spectral_profiles->add_stats_types(CARTA::StatsType::Extrema);
        return set_spectral_requirements;
    }

    static bool SetRegion(carta::RegionHandler& region_handler, int file_id, int& region_id, const std::vector<float>& points,
        std::shared_ptr<casacore::CoordinateSystem> csys, bool is_annotation) {
        std::vector<CARTA::Point> control_points;
        for (auto i = 0; i < points.size(); i += 2) {
            control_points.push_back(Message::Point(points[i], points[i + 1]));
        }

        // Define RegionState and set region
        CARTA::RegionType region_type;
        if (control_points.size() > 1) {
            region_type = is_annotation ? CARTA::ANNPOLYGON : CARTA::POLYGON;
        } else {
            region_type = is_annotation ? CARTA::ANNPOINT : CARTA::POINT;
        }
        RegionState region_state(file_id, region_type, control_points, 0.0);
        return region_handler.SetRegion(region_id, region_state, csys);
    }

    static bool SpectralProfile(const fs::path& image_path, const std::vector<float>& points, CARTA::SpectralProfileData& spectral_data,
        bool is_annotation = false) {
        auto loader = carta::FileLoader::GetLoader(image_path);
        std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));
        carta::RegionHandler region_handler;

        // Set polygon or point region
        int file_id(0), region_id(-1);
        auto csys = frame->CoordinateSystem();
        if (!SetRegion(region_handler, file_id, region_id, points, csys, is_annotation)) {
            return false;
        }

        // Set spectral requirements (Message requests 10 stats types)
        auto spectral_req_message = SetSpectralRequirements(file_id, region_id, "z");
        std::vector<CARTA::SetSpectralRequirements_SpectralConfig> spectral_requirements = {
            spectral_req_message.spectral_profiles().begin(), spectral_req_message.spectral_profiles().end()};
        if (!region_handler.SetSpectralRequirements(region_id, file_id, frame, spectral_requirements)) {
            return false;
        }

        // Get spectral profile
        return region_handler.FillSpectralProfileData(
            [&](CARTA::SpectralProfileData profile_data) { spectral_data = profile_data; }, region_id, file_id, false);
    }

    static std::vector<double> GetExpectedMeanProfile(fs::path& image_path, int num_channels, CARTA::RegionType type) {
        // Read image for profile for box region blc (0,0) trc (3,3) or point (3,3)
        FitsDataReader reader(image_path);
        std::vector<double> profile;
        if (type == CARTA::POLYGON) {
            for (hsize_t i = 0; i < num_channels; ++i) {
                auto channel_data = reader.ReadRegion({0, 0, i}, {4, 4, i + 1});
                double sum = std::accumulate(channel_data.begin(), channel_data.end(), 0.0);
                double mean = sum / channel_data.size();
                profile.push_back(mean);
            }
        } else {
            hsize_t chan = num_channels;
            auto channel_data = reader.ReadRegion({3, 3, 0}, {4, 4, chan});
            for (float data : channel_data) {
                profile.push_back(data);
            }
        }
        return profile;
    }
};

TEST_F(RegionSpectralProfileTest, TestPolygonSpectralProfile) {
    // Box described as 4-corner polygon
    auto image_path = FitsImages() / "noise_3d.fits";
    int num_channels = 10;
    std::vector<float> points = {0.0, 0.0, 0.0, 3.0, 3.0, 3.0, 3.0, 0.0};
    CARTA::SpectralProfileData spectral_data;
    bool ok = SpectralProfile(image_path, points, spectral_data);

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
        auto values = profile.raw_values_fp64();    // string (bytes)
        ASSERT_EQ(values.size(), num_channels * 8); // double (8-byte) values

        if (profile.stats_type() == CARTA::StatsType::Mean) {
            auto expected_profile = GetExpectedMeanProfile(image_path, num_channels, CARTA::POLYGON);
            auto actual_profile = GetSpectralProfileValues<double>(profile);
            CmpVectors<double>(actual_profile, expected_profile);
        }
    }
}

TEST_F(RegionSpectralProfileTest, TestAnnPolygonSpectralProfile) {
    auto image_path = FitsImages() / "noise_3d.fits";
    std::vector<float> points = {0.0, 0.0, 0.0, 3.0, 3.0, 3.0, 3.0, 0.0};
    CARTA::SpectralProfileData spectral_data;
    bool ok = SpectralProfile(image_path, points, spectral_data, true);
    ASSERT_FALSE(ok);
}

TEST_F(RegionSpectralProfileTest, TestPointSpectralProfile) {
    auto image_path = FitsImages() / "noise_3d.fits";
    int num_channels = 10;
    std::vector<float> points = {3.0, 3.0};
    CARTA::SpectralProfileData spectral_data;
    bool ok = SpectralProfile(image_path, points, spectral_data);

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
        auto values = profile.raw_values_fp64();    // string (bytes)
        ASSERT_EQ(values.size(), num_channels * 8); // double (8-byte) values

        if (profile.stats_type() == CARTA::StatsType::Mean) {
            auto expected_profile = GetExpectedMeanProfile(image_path, num_channels, CARTA::POINT);
            auto actual_profile = GetSpectralProfileValues<double>(profile);
            CmpVectors<double>(actual_profile, expected_profile);
        }
    }
}

TEST_F(RegionSpectralProfileTest, TestAnnPointSpectralProfile) {
    auto image_path = FitsImages() / "noise_3d.fits";
    std::vector<float> points = {3.0, 3.0};
    CARTA::SpectralProfileData spectral_data;
    bool ok = SpectralProfile(image_path, points, spectral_data, true);
    ASSERT_FALSE(ok);
}

class ComputedStokesSpectralProfileTest : public RegionSpectralProfileTest, public ::testing::WithParamInterface<std::string> {
public:
    void SetUp() override {
        // DataReader has no virtual destructor, so each reader is kept with its own type
        if (GetParam() == "fits") {
            _image_path = FitsImages() / "noise_iquv.fits";
            _fits_reader = std::make_unique<FitsDataReader>(_image_path);
            _reader = _fits_reader.get();
        } else {
            _image_path = Hdf5Images() / "noise_iquv.hdf5";
            _hdf5_reader = std::make_unique<Hdf5DataReader>(_image_path);
            _reader = _hdf5_reader.get();
        }
        _frame = std::make_shared<Frame>(0, carta::FileLoader::GetLoader(_image_path), "0");
        _csys = _frame->CoordinateSystem();
        // The tests use 4 x 4 boxes. Only the HDF5 image reads their profiles through the loader.
        ASSERT_EQ(_frame->UseLoaderSpectralData(casacore::IPosition(2, 4, 4)), GetParam() == "hdf5");
    }

    // Sets a box region with corners (x0, y0) and (x1, y1), or moves it there
    bool SetBox(float x0, float y0, float x1, float y1) {
        return SetRegion(_region_handler, _file_id, _region_id, {x0, y0, x0, y1, x1, y1, x1, y0}, _csys, false);
    }

    bool SetCoordinate(const std::string& coordinate) {
        auto message = SetSpectralRequirements(_file_id, _region_id, coordinate);
        std::vector<CARTA::SetSpectralRequirements_SpectralConfig> requirements = {
            message.spectral_profiles().begin(), message.spectral_profiles().end()};
        return _region_handler.SetSpectralRequirements(_region_id, _file_id, _frame, requirements);
    }

    bool GetProfile(CARTA::SpectralProfileData& spectral_data) {
        spectral_data.Clear();
        return _region_handler.FillSpectralProfileData(
            [&](CARTA::SpectralProfileData profile_data) { spectral_data = profile_data; }, _region_id, _file_id, false);
    }

    // Returns the mean of each channel of stokes `stokes` in the box from (x0, y0) to (x1, y1)
    std::vector<double> MeanProfile(hsize_t stokes, hsize_t x0, hsize_t y0, hsize_t x1, hsize_t y1) {
        std::vector<double> profile;
        for (hsize_t z = 0; z < DEPTH; ++z) {
            auto data = _reader->ReadRegion({x0, y0, z, stokes}, {x1 + 1, y1 + 1, z + 1, stokes + 1});
            profile.push_back(std::accumulate(data.begin(), data.end(), 0.0) / data.size());
        }
        return profile;
    }

    // Returns the Plinear mean profile of the box. RegionHandler combines the profiles of Q and U.
    std::vector<double> PlinearMeanProfile(hsize_t x0, hsize_t y0, hsize_t x1, hsize_t y1) {
        auto q = MeanProfile(1, x0, y0, x1, y1);
        auto u = MeanProfile(2, x0, y0, x1, y1);
        std::vector<double> profile;
        for (size_t z = 0; z < q.size(); ++z) {
            profile.push_back(std::sqrt(q[z] * q[z] + u[z] * u[z]));
        }
        return profile;
    }

    static std::vector<double> MeanValues(const CARTA::SpectralProfileData& spectral_data) {
        for (const auto& profile : spectral_data.profiles()) {
            if (profile.stats_type() == CARTA::StatsType::Mean) {
                return GetSpectralProfileValues<double>(profile);
            }
        }
        return {};
    }

    // The image noise_iquv has 5 channels
    static constexpr hsize_t DEPTH = 5;
    fs::path _image_path;
    std::unique_ptr<FitsDataReader> _fits_reader;
    std::unique_ptr<Hdf5DataReader> _hdf5_reader;
    DataReader* _reader = nullptr;
    std::shared_ptr<Frame> _frame;
    std::shared_ptr<casacore::CoordinateSystem> _csys;
    carta::RegionHandler _region_handler;
    int _file_id = 0;
    int _region_id = -1;
};

// The FITS image goes through casacore. The HDF5 image goes through the loader, because it has swizzled data.
TEST_P(ComputedStokesSpectralProfileTest, PlinearProfileFollowsRegion) {
    CARTA::SpectralProfileData spectral_data;
    ASSERT_TRUE(SetBox(0, 0, 3, 3));
    ASSERT_TRUE(SetCoordinate("Plinearz"));
    ASSERT_TRUE(GetProfile(spectral_data));
    EXPECT_EQ(spectral_data.stokes(), CARTA::PolarizationType::Plinear);
    EXPECT_EQ(spectral_data.progress(), 1.0);
    CmpVectors<double>(MeanValues(spectral_data), PlinearMeanProfile(0, 0, 3, 3), 1e-6);

    ASSERT_TRUE(SetBox(4, 4, 7, 7));
    ASSERT_TRUE(GetProfile(spectral_data));
    EXPECT_EQ(spectral_data.progress(), 1.0);
    CmpVectors<double>(MeanValues(spectral_data), PlinearMeanProfile(4, 4, 7, 7), 1e-6);
}

// A profile of Q first caches the LCRegion of the image. Moving the region clears that cache.
TEST_P(ComputedStokesSpectralProfileTest, PlinearProfileAfterQProfileFollowsRegion) {
    CARTA::SpectralProfileData spectral_data;
    ASSERT_TRUE(SetBox(0, 0, 3, 3));
    ASSERT_TRUE(SetCoordinate("Qz"));
    ASSERT_TRUE(GetProfile(spectral_data));
    ASSERT_TRUE(SetCoordinate("Plinearz"));
    ASSERT_TRUE(GetProfile(spectral_data));
    EXPECT_EQ(spectral_data.stokes(), CARTA::PolarizationType::Plinear);
    EXPECT_EQ(spectral_data.progress(), 1.0);
    CmpVectors<double>(MeanValues(spectral_data), PlinearMeanProfile(0, 0, 3, 3), 1e-6);

    ASSERT_TRUE(SetBox(4, 4, 7, 7));
    ASSERT_TRUE(GetProfile(spectral_data));
    EXPECT_EQ(spectral_data.progress(), 1.0);
    CmpVectors<double>(MeanValues(spectral_data), PlinearMeanProfile(4, 4, 7, 7), 1e-6);
}

INSTANTIATE_TEST_SUITE_P(RegionSpectralProfileTest, ComputedStokesSpectralProfileTest, ::testing::Values("fits", "hdf5"),
    [](const ::testing::TestParamInfo<std::string>& info) { return info.param; });
