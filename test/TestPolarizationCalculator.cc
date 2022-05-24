/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>
#include <math.h>

#include "CommonTestUtilities.h"
#include "Frame/Frame.h"
#include "ImageData/PolarizationCalculator.h"
#include "Region/RegionHandler.h"
#include "Session/Session.h"

using namespace carta;

static const std::string IMAGE_SHAPE = "100 100 25 4";
static const std::string IMAGE_OPTS = "-s 0 -n row column channel -d 5";

static const std::set<int> COMPUTED_STOKES{
    COMPUTE_STOKES_PTOTAL, COMPUTE_STOKES_PFTOTAL, COMPUTE_STOKES_PLINEAR, COMPUTE_STOKES_PFLINEAR, COMPUTE_STOKES_PANGLE};

static std::unordered_map<std::string, int> STOKES_INDICES{{"I", 0}, {"Q", 1}, {"U", 2}, {"V", 3}};

static std::unordered_map<std::string, std::vector<float>> DATA{
    {"I", std::vector<float>()}, {"Q", std::vector<float>()}, {"U", std::vector<float>()}, {"V", std::vector<float>()}};

class PolarizationCalculatorTest : public ::testing::Test {
public:
    class TestFrame : public Frame {
    public:
        TestFrame(uint32_t session_id, std::shared_ptr<carta::FileLoader> loader, const std::string& hdu, int default_z = DEFAULT_Z)
            : Frame(session_id, loader, hdu, default_z) {}

        static void TestFrameImageCache(std::string sample_file_path) {
            // Open an image file
            std::shared_ptr<casacore::ImageInterface<float>> image;
            EXPECT_TRUE(OpenImage(image, sample_file_path));

            if (image->ndim() < 4) {
                spdlog::error("Invalid image dimension.");
                return;
            }

            // Get spectral axis size
            casacore::CoordinateSystem coord_sys = image->coordinates();
            int spectral_axis = coord_sys.spectralAxisNumber();
            if (spectral_axis < 0) {
                spectral_axis = 2; // assume spectral axis
            }
            int spectral_axis_size = image->shape()[spectral_axis];

            // Open an image file through Frame
            LoaderCache loaders(LOADER_CACHE_SIZE);
            std::unique_ptr<TestFrame> frame(new TestFrame(0, loaders.Get(sample_file_path), "0"));
            EXPECT_TRUE(frame->IsValid());
            EXPECT_TRUE(frame->_open_image_error.empty());

            // Calculate stokes data
            std::string message;
            std::vector<float> data;
            for (int channel = 0; channel < spectral_axis_size; ++channel) {
                for (const auto& stokes : COMPUTED_STOKES) {
                    frame->SetImageChannels(channel, stokes, message);
                    GetImageData(data, frame->_image_cache.get(), frame->_image_cache_size);
                    CheckImageCache(image, channel, stokes, data);
                }
            }
        }

        static void GetImageData(std::vector<float>& data, const float* data_ptr, size_t data_size) {
            data.resize(data_size);
            for (int i = 0; i < data_size; ++i) {
                data[i] = data_ptr[i];
            }
        }
    };

    static void CheckImageCache(
        const std::shared_ptr<casacore::ImageInterface<float>>& image, int channel, int stokes, const std::vector<float>& data) {
        if (image->ndim() < 4) {
            spdlog::error("Invalid image dimension.");
            return;
        }

        // Get stokes data I, Q, U, and V
        for (const auto& one : STOKES_INDICES) {
            auto stokes_type = one.first;
            GetImageData(DATA[stokes_type], image, STOKES_INDICES[stokes_type], AxisRange(channel));
        }

        EXPECT_TRUE((data.size() == DATA["I"].size()) && (data.size() == DATA["Q"].size()) && (data.size() == DATA["U"].size()) &&
                    (data.size() == DATA["V"].size()));

        // Verify each pixel value from calculation results
        for (int i = 0; i < data.size(); ++i) {
            if (!isnan(data[i])) {
                if (stokes == COMPUTE_STOKES_PTOTAL) {
                    auto total_polarized_intensity = sqrt(pow(DATA["Q"][i], 2) + pow(DATA["U"][i], 2) + pow(DATA["V"][i], 2));
                    EXPECT_FLOAT_EQ(data[i], total_polarized_intensity);
                } else if (stokes == COMPUTE_STOKES_PFTOTAL) {
                    auto total_fractional_polarized_intensity =
                        100.0 * sqrt(pow(DATA["Q"][i], 2) + pow(DATA["U"][i], 2) + pow(DATA["V"][i], 2)) / DATA["I"][i];
                    EXPECT_FLOAT_EQ(data[i], total_fractional_polarized_intensity);
                } else if (stokes == COMPUTE_STOKES_PLINEAR) {
                    auto polarized_intensity = sqrt(pow(DATA["Q"][i], 2) + pow(DATA["U"][i], 2));
                    EXPECT_FLOAT_EQ(data[i], polarized_intensity);
                } else if (stokes == COMPUTE_STOKES_PFLINEAR) {
                    auto fractional_polarized_intensity = 100.0 * sqrt(pow(DATA["Q"][i], 2) + pow(DATA["U"][i], 2)) / DATA["I"][i];
                    EXPECT_FLOAT_EQ(data[i], fractional_polarized_intensity);
                } else if (stokes == COMPUTE_STOKES_PANGLE) {
                    auto polarized_angle = (180.0 / casacore::C::pi) * atan2(DATA["U"][i], DATA["Q"][i]) / 2;
                    EXPECT_FLOAT_EQ(data[i], polarized_angle);
                }
            }
        }
    }

    static std::pair<std::vector<float>, std::vector<float>> GetCursorSpatialProfiles(
        const std::shared_ptr<casacore::ImageInterface<float>>& image, int channel, int stokes, int cursor_x, int cursor_y) {
        if (image->ndim() < 4) {
            spdlog::error("Invalid image dimension.");
            return std::make_pair(std::vector<float>(), std::vector<float>());
        }

        // Get spectral axis size
        int x_size = image->shape()[0];

        // Get stokes data I, Q, U, and V
        for (const auto& one : STOKES_INDICES) {
            auto stokes_type = one.first;
            GetImageData(DATA[stokes_type], image, STOKES_INDICES[stokes_type], AxisRange(channel));
        }

        // Get profile x
        std::vector<float> profile_x;
        for (int i = 0; i < DATA["I"].size(); ++i) {
            if (((i / x_size) == cursor_y) && ((i % x_size) < x_size)) {
                if (stokes == COMPUTE_STOKES_PTOTAL) {
                    auto total_polarized_intensity = sqrt(pow(DATA["Q"][i], 2) + pow(DATA["U"][i], 2) + pow(DATA["V"][i], 2));
                    profile_x.push_back(total_polarized_intensity);
                } else if (stokes == COMPUTE_STOKES_PFTOTAL) {
                    auto total_fractional_polarized_intensity =
                        100.0 * sqrt(pow(DATA["Q"][i], 2) + pow(DATA["U"][i], 2) + pow(DATA["V"][i], 2)) / DATA["I"][i];
                    profile_x.push_back(total_fractional_polarized_intensity);
                } else if (stokes == COMPUTE_STOKES_PLINEAR) {
                    auto polarized_intensity = sqrt(pow(DATA["Q"][i], 2) + pow(DATA["U"][i], 2));
                    profile_x.push_back(polarized_intensity);
                } else if (stokes == COMPUTE_STOKES_PFLINEAR) {
                    auto fractional_polarized_intensity = 100.0 * sqrt(pow(DATA["Q"][i], 2) + pow(DATA["U"][i], 2)) / DATA["I"][i];
                    profile_x.push_back(fractional_polarized_intensity);
                } else if (stokes == COMPUTE_STOKES_PANGLE) {
                    auto polarized_angle = (180.0 / casacore::C::pi) * atan2(DATA["U"][i], DATA["Q"][i]) / 2;
                    profile_x.push_back(polarized_angle);
                } else if (stokes == 0) {
                    profile_x.push_back(DATA["I"][i]);
                } else if (stokes == 1) {
                    profile_x.push_back(DATA["Q"][i]);
                } else if (stokes == 2) {
                    profile_x.push_back(DATA["U"][i]);
                } else if (stokes == 3) {
                    profile_x.push_back(DATA["V"][i]);
                } else {
                    spdlog::error("Unknown stokes: {}", stokes);
                }
            }
        }

        // Get profile y
        std::vector<float> profile_y;
        for (int i = 0; i < DATA["I"].size(); ++i) {
            if ((i % x_size) == cursor_x) {
                if (stokes == COMPUTE_STOKES_PTOTAL) {
                    auto total_polarized_intensity = sqrt(pow(DATA["Q"][i], 2) + pow(DATA["U"][i], 2) + pow(DATA["V"][i], 2));
                    profile_y.push_back(total_polarized_intensity);
                } else if (stokes == COMPUTE_STOKES_PFTOTAL) {
                    auto total_fractional_polarized_intensity =
                        100.0 * sqrt(pow(DATA["Q"][i], 2) + pow(DATA["U"][i], 2) + pow(DATA["V"][i], 2)) / DATA["I"][i];
                    profile_y.push_back(total_fractional_polarized_intensity);
                } else if (stokes == COMPUTE_STOKES_PLINEAR) {
                    auto polarized_intensity = sqrt(pow(DATA["Q"][i], 2) + pow(DATA["U"][i], 2));
                    profile_y.push_back(polarized_intensity);
                } else if (stokes == COMPUTE_STOKES_PFLINEAR) {
                    auto polarized_intensity = 100.0 * sqrt(pow(DATA["Q"][i], 2) + pow(DATA["U"][i], 2)) / DATA["I"][i];
                    profile_y.push_back(polarized_intensity);
                } else if (stokes == COMPUTE_STOKES_PANGLE) {
                    auto polarized_angle = (180.0 / casacore::C::pi) * atan2(DATA["U"][i], DATA["Q"][i]) / 2;
                    profile_y.push_back(polarized_angle);
                } else if (stokes == 0) {
                    profile_y.push_back(DATA["I"][i]);
                } else if (stokes == 1) {
                    profile_y.push_back(DATA["Q"][i]);
                } else if (stokes == 2) {
                    profile_y.push_back(DATA["U"][i]);
                } else if (stokes == 3) {
                    profile_y.push_back(DATA["V"][i]);
                } else {
                    spdlog::error("Unknown stokes: {}", stokes);
                }
            }
        }
        return std::make_pair(profile_x, profile_y);
    }

    static std::vector<float> GetCursorSpectralProfiles(
        const std::shared_ptr<casacore::ImageInterface<float>>& image, AxisRange z_range, int stokes, int cursor_x, int cursor_y) {
        if (image->ndim() < 4) {
            spdlog::error("Invalid image dimension.");
            return std::vector<float>();
        }

        // Get stokes data I, Q, U, and V
        for (const auto& one : STOKES_INDICES) {
            auto stokes_type = one.first;
            GetImageData(DATA[stokes_type], image, STOKES_INDICES[stokes_type], z_range, AxisRange(cursor_x), AxisRange(cursor_y));
        }

        // Get profile z
        std::vector<float> profile;
        for (int i = 0; i < DATA["I"].size(); ++i) {
            if (stokes == COMPUTE_STOKES_PTOTAL) {
                auto total_polarized_intensity = sqrt(pow(DATA["Q"][i], 2) + pow(DATA["U"][i], 2) + pow(DATA["V"][i], 2));
                profile.push_back(total_polarized_intensity);
            } else if (stokes == COMPUTE_STOKES_PFTOTAL) {
                auto total_fractional_polarized_intensity =
                    100.0 * sqrt(pow(DATA["Q"][i], 2) + pow(DATA["U"][i], 2) + pow(DATA["V"][i], 2)) / DATA["I"][i];
                profile.push_back(total_fractional_polarized_intensity);
            } else if (stokes == COMPUTE_STOKES_PLINEAR) {
                auto polarized_intensity = sqrt(pow(DATA["Q"][i], 2) + pow(DATA["U"][i], 2));
                profile.push_back(polarized_intensity);
            } else if (stokes == COMPUTE_STOKES_PFLINEAR) {
                auto fractional_polarized_intensity = 100.0 * sqrt(pow(DATA["Q"][i], 2) + pow(DATA["U"][i], 2)) / DATA["I"][i];
                profile.push_back(fractional_polarized_intensity);
            } else if (stokes == COMPUTE_STOKES_PANGLE) {
                auto polarized_angle = (180.0 / casacore::C::pi) * atan2(DATA["U"][i], DATA["Q"][i]) / 2;
                profile.push_back(polarized_angle);
            } else if (stokes == 0) {
                profile.push_back(DATA["I"][i]);
            } else if (stokes == 1) {
                profile.push_back(DATA["Q"][i]);
            } else if (stokes == 2) {
                profile.push_back(DATA["U"][i]);
            } else if (stokes == 3) {
                profile.push_back(DATA["V"][i]);
            } else {
                spdlog::error("Unknown stokes: {}", stokes);
            }
        }
        return profile;
    }

    static void TestCursorProfiles(int current_channel, int current_stokes, int config_stokes, std::string stokes_config_x,
        std::string stokes_config_y, std::string stokes_config_z) {
        auto fits_file_path = ImageGenerator::GeneratedFitsImagePath(IMAGE_SHAPE, IMAGE_OPTS);
        auto hdf5_file_path = ImageGenerator::GeneratedHdf5ImagePath(IMAGE_SHAPE, IMAGE_OPTS);

        // Open an image file
        std::shared_ptr<casacore::ImageInterface<float>> image;
        EXPECT_TRUE(OpenImage(image, fits_file_path));

        // Open an image file through Frame
        LoaderCache loaders(LOADER_CACHE_SIZE);
        std::unique_ptr<Frame> frame(new Frame(0, loaders.Get(hdf5_file_path), "0"));
        EXPECT_TRUE(frame->IsValid());

        // Set spatial spatial_configs requirements
        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> spatial_configs = {
            Message::SpatialConfig(stokes_config_x), Message::SpatialConfig(stokes_config_y)};
        frame->SetSpatialRequirements(spatial_configs);

        // Get directional axis size
        int cursor_x(image->shape()[0] / 2);
        int cursor_y(image->shape()[1] / 2);
        frame->SetCursor(cursor_x, cursor_y);

        std::string message;
        frame->SetImageChannels(current_channel, current_stokes, message);

        // Get spatial spatial_configs from the Frame
        std::vector<CARTA::SpatialProfileData> spatial_profiles1;
        frame->FillSpatialProfileData(spatial_profiles1);

        // Get spatial spatial_configs in another way
        auto spatial_profiles2 = GetCursorSpatialProfiles(image, current_channel, config_stokes, cursor_x, cursor_y);

        // Check the consistency of two ways
        CmpSpatialProfiles(spatial_profiles1, spatial_profiles2);

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

        auto spectral_profile_as_float1 = GetSpectralProfileValues<float>(spectral_profile);

        // Get spatial spatial_configs in another way
        int stokes = (stokes_config_z == "z") ? current_stokes : config_stokes;
        std::vector<float> spectral_profile_as_float2 = GetCursorSpectralProfiles(image, AxisRange(ALL_Z), stokes, cursor_x, cursor_y);

        // Check the consistency of two ways
        CmpVectors(spectral_profile_as_float1, spectral_profile_as_float2);
    }

    static void TestPointRegionProfiles(int current_channel, int current_stokes, int config_stokes, std::string stokes_config_x,
        std::string stokes_config_y, std::string stokes_config_z) {
        auto fits_file_path = ImageGenerator::GeneratedFitsImagePath(IMAGE_SHAPE, IMAGE_OPTS);
        auto hdf5_file_path = ImageGenerator::GeneratedHdf5ImagePath(IMAGE_SHAPE, IMAGE_OPTS);

        // Open a reference image file
        std::shared_ptr<casacore::ImageInterface<float>> image;
        EXPECT_TRUE(OpenImage(image, fits_file_path));

        // Open an experimental image through the Frame
        int file_id(0);
        LoaderCache loaders(LOADER_CACHE_SIZE);
        auto frame = std::make_shared<Frame>(file_id, loaders.Get(hdf5_file_path), "0");
        EXPECT_TRUE(frame->IsValid());

        // Set image channels through the Frame
        std::string message;
        frame->SetImageChannels(current_channel, current_stokes, message);

        // Create a region handler
        auto region_handler = std::make_unique<carta::RegionHandler>();

        // Set a point region state
        int region_id(1);
        int cursor_x = image->shape()[0] / 2;
        int cursor_y = image->shape()[1] / 2;
        std::vector<CARTA::Point> points = {Message::Point(cursor_x, cursor_y)};

        RegionState region_state(file_id, CARTA::RegionType::POINT, points, 0);
        EXPECT_TRUE(region_handler->SetRegion(region_id, region_state, frame->CoordinateSystem()));

        // Set spatial requirements for a point region
        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {
            Message::SpatialConfig(stokes_config_x), Message::SpatialConfig(stokes_config_y)};
        region_handler->SetSpatialRequirements(region_id, file_id, frame, profiles);

        // Get a point region spatial profiles
        std::vector<CARTA::SpatialProfileData> spatial_profiles1;
        region_handler->FillPointSpatialProfileData(file_id, region_id, spatial_profiles1);

        // Get a point region spatial profiles in another way
        auto spatial_profiles2 = GetCursorSpatialProfiles(image, current_channel, config_stokes, cursor_x, cursor_y);

        // Compare data
        CmpSpatialProfiles(spatial_profiles1, spatial_profiles2);

        // Set spectral configs for a point region
        std::vector<CARTA::SetSpectralRequirements_SpectralConfig> spectral_configs{Message::SpectralConfig(stokes_config_z)};
        region_handler->SetSpectralRequirements(region_id, file_id, frame, spectral_configs);

        // Get cursor spectral profile data from the RegionHandler
        CARTA::SpectralProfile spectral_profile;
        bool stokes_changed = (stokes_config_z == "z");

        region_handler->FillSpectralProfileData(
            [&](CARTA::SpectralProfileData tmp_spectral_profile) {
                if (tmp_spectral_profile.progress() >= 1.0) {
                    spectral_profile = tmp_spectral_profile.profiles(0);
                }
            },
            region_id, file_id, stokes_changed);

        auto spectral_profile_as_double = GetSpectralProfileValues<double>(spectral_profile);
        // convert the double type vector to the float type vector
        std::vector<float> spectral_profile_as_float1(spectral_profile_as_double.begin(), spectral_profile_as_double.end());

        // Get spectral profiles in another way
        int stokes = (stokes_config_z == "z") ? current_stokes : config_stokes;
        std::vector<float> spectral_profile_as_float2 = GetCursorSpectralProfiles(image, AxisRange(ALL_Z), stokes, cursor_x, cursor_y);

        // Check the consistency of two ways
        CmpVectors(spectral_profile_as_float1, spectral_profile_as_float2);
    }

    static void CalculateCubeHistogram(
        const std::shared_ptr<Frame>& frame, int current_channel, int current_stokes, carta::Histogram& cube_histogram) {
        // Set image channels (cube histogram should be independent of z channel settings)
        std::string message;
        frame->SetImageChannels(current_channel, current_stokes, message);

        int stokes;
        std::string coordinate;
        frame->GetStokesTypeIndex(coordinate, stokes); // get current stokes
        size_t depth(frame->Depth());

        // stats for entire cube
        carta::BasicStats<float> cube_stats;
        for (size_t z = 0; z < depth; ++z) {
            carta::BasicStats<float> z_stats;
            if (!frame->GetBasicStats(z, stokes, z_stats)) {
                spdlog::error("Failed to get statistics data form the cube histogram calculation.");
                return;
            }
            cube_stats.join(z_stats);
        }

        // get histogram bins for each z and accumulate bin counts in cube_bins
        carta::Histogram z_histogram;
        for (size_t z = 0; z < depth; ++z) {
            if (!frame->CalculateHistogram(CUBE_REGION_ID, z, stokes, -1, cube_stats, z_histogram)) {
                spdlog::error("Failed to calculate the cube histogram.");
                return;
            }
            if (z == 0) {
                cube_histogram = std::move(z_histogram);
            } else {
                cube_histogram.Add(z_histogram);
            }
        }
    }

    static void TestCubeHistogram(int current_stokes) {
        auto fits_file_path = ImageGenerator::GeneratedFitsImagePath(IMAGE_SHAPE, IMAGE_OPTS);
        auto hdf5_file_path = ImageGenerator::GeneratedHdf5ImagePath(IMAGE_SHAPE, IMAGE_OPTS);

        // Open a reference image file
        std::shared_ptr<casacore::ImageInterface<float>> image;
        EXPECT_TRUE(OpenImage(image, fits_file_path));

        // Calculate the cube histogram
        LoaderCache loaders(LOADER_CACHE_SIZE);
        auto frame1 = std::make_shared<Frame>(0, loaders.Get(hdf5_file_path), "0");
        carta::Histogram cube_histogram1;
        CalculateCubeHistogram(frame1, 0, current_stokes, cube_histogram1);

        // Calculate the cube histogram in another way
        carta::PolarizationCalculator polarization_calculator(image);
        std::shared_ptr<casacore::ImageInterface<float>> resulting_image;
        if (current_stokes == COMPUTE_STOKES_PTOTAL) {
            resulting_image = polarization_calculator.ComputeTotalPolarizedIntensity();
        } else if (current_stokes == COMPUTE_STOKES_PFTOTAL) {
            resulting_image = polarization_calculator.ComputeTotalFractionalPolarizedIntensity();
        } else if (current_stokes == COMPUTE_STOKES_PLINEAR) {
            resulting_image = polarization_calculator.ComputePolarizedIntensity();
        } else if (current_stokes == COMPUTE_STOKES_PFLINEAR) {
            resulting_image = polarization_calculator.ComputeFractionalPolarizedIntensity();
        } else if (current_stokes == COMPUTE_STOKES_PANGLE) {
            resulting_image = polarization_calculator.ComputePolarizedAngle();
        } else {
            spdlog::error("Unknown computed stokes type: {}", current_stokes);
            return;
        }

        auto loader = std::shared_ptr<carta::FileLoader>(carta::FileLoader::GetLoader(resulting_image));
        auto frame2 = std::make_shared<Frame>(1, loader, "");
        carta::Histogram cube_histogram2;
        CalculateCubeHistogram(frame2, 1, 0, cube_histogram2);

        EXPECT_TRUE(CmpHistograms(cube_histogram1, cube_histogram2));
    }
};

TEST_F(PolarizationCalculatorTest, TestStokesSource) {
    StokesSource stokes_source_1(0, AxisRange(0));
    StokesSource stokes_source_2(1, AxisRange(0));
    StokesSource stokes_source_3(0, AxisRange(1));
    StokesSource stokes_source_4(0, AxisRange(0));

    StokesSource stokes_source_5(0, AxisRange(0, 10));
    StokesSource stokes_source_6(0, AxisRange(0, 10));
    StokesSource stokes_source_7(1, AxisRange(0, 10));
    StokesSource stokes_source_8(1, AxisRange(0, 5));

    EXPECT_TRUE(stokes_source_1 != stokes_source_2);
    EXPECT_TRUE(stokes_source_1 != stokes_source_3);
    EXPECT_TRUE(stokes_source_1 == stokes_source_4);

    EXPECT_TRUE(stokes_source_1 != stokes_source_5);
    EXPECT_TRUE(stokes_source_5 == stokes_source_6);
    EXPECT_TRUE(stokes_source_6 != stokes_source_7);
    EXPECT_TRUE(stokes_source_7 != stokes_source_8);

    StokesSource stokes_source_9 = stokes_source_8;

    EXPECT_TRUE(stokes_source_9 == stokes_source_8);
    EXPECT_TRUE(stokes_source_9 != stokes_source_7);

    StokesSource stokes_source_10 = StokesSource();
    StokesSource stokes_source_11 = stokes_source_10;

    EXPECT_TRUE(stokes_source_10.IsOriginalImage());
    EXPECT_TRUE(stokes_source_10 != stokes_source_1);
    EXPECT_TRUE(stokes_source_10 == stokes_source_11);

    StokesSource stokes_source_12(0, AxisRange(0), AxisRange(0), AxisRange(0));
    StokesSource stokes_source_13(1, AxisRange(0), AxisRange(1), AxisRange(0));
    StokesSource stokes_source_14(0, AxisRange(1), AxisRange(0, 1), AxisRange(0, 1));
    StokesSource stokes_source_15(0, AxisRange(1), AxisRange(0, 1), AxisRange(0, 1));

    EXPECT_TRUE(stokes_source_12 != stokes_source_13);
    EXPECT_TRUE(stokes_source_12 != stokes_source_14);
    EXPECT_TRUE(stokes_source_13 != stokes_source_14);
    EXPECT_TRUE(stokes_source_14 == stokes_source_15);
}

TEST_F(PolarizationCalculatorTest, TestFrameImageCache) {
    TestFrame::TestFrameImageCache(ImageGenerator::GeneratedFitsImagePath(IMAGE_SHAPE));
    TestFrame::TestFrameImageCache(ImageGenerator::GeneratedFitsImagePath(IMAGE_SHAPE, IMAGE_OPTS));
}

TEST_F(PolarizationCalculatorTest, TestCursorProfiles) {
    TestCursorProfiles(0, 0, COMPUTE_STOKES_PTOTAL, "Ptotalx", "Ptotaly", "Ptotalz");
    TestCursorProfiles(0, 0, COMPUTE_STOKES_PFTOTAL, "PFtotalx", "PFtotaly", "PFtotalz");
    TestCursorProfiles(0, 0, COMPUTE_STOKES_PLINEAR, "Plinearx", "Plineary", "Plinearz");
    TestCursorProfiles(0, 0, COMPUTE_STOKES_PFLINEAR, "PFlinearx", "PFlineary", "PFlinearz");
    TestCursorProfiles(0, 0, COMPUTE_STOKES_PANGLE, "Panglex", "Pangley", "Panglez");

    TestCursorProfiles(0, 0, 0, "Ix", "Iy", "Iz");
    TestCursorProfiles(0, 0, 1, "Qx", "Qy", "Qz");
    TestCursorProfiles(0, 0, 2, "Ux", "Uy", "Uz");
    TestCursorProfiles(0, 0, 3, "Vx", "Vy", "Vz");

    TestCursorProfiles(0, 0, COMPUTE_STOKES_PTOTAL, "Ptotalx", "Ptotaly", "z");
    TestCursorProfiles(0, 0, COMPUTE_STOKES_PFTOTAL, "PFtotalx", "PFtotaly", "z");
    TestCursorProfiles(0, 0, COMPUTE_STOKES_PLINEAR, "Plinearx", "Plineary", "z");
    TestCursorProfiles(0, 0, COMPUTE_STOKES_PFLINEAR, "PFlinearx", "PFlineary", "z");
    TestCursorProfiles(0, 0, COMPUTE_STOKES_PANGLE, "Panglex", "Pangley", "z");

    TestCursorProfiles(0, 0, 0, "Ix", "Iy", "z");
    TestCursorProfiles(0, 0, 1, "Qx", "Qy", "z");
    TestCursorProfiles(0, 0, 2, "Ux", "Uy", "z");
    TestCursorProfiles(0, 0, 3, "Vx", "Vy", "z");
}

TEST_F(PolarizationCalculatorTest, TestPointRegionProfiles) {
    TestPointRegionProfiles(0, 0, COMPUTE_STOKES_PTOTAL, "Ptotalx", "Ptotaly", "Ptotalz");
    TestPointRegionProfiles(0, 0, COMPUTE_STOKES_PFTOTAL, "PFtotalx", "PFtotaly", "PFtotalz");
    TestPointRegionProfiles(0, 0, COMPUTE_STOKES_PLINEAR, "Plinearx", "Plineary", "Plinearz");
    TestPointRegionProfiles(0, 0, COMPUTE_STOKES_PFLINEAR, "PFlinearx", "PFlineary", "PFlinearz");
    TestPointRegionProfiles(0, 0, COMPUTE_STOKES_PANGLE, "Panglex", "Pangley", "Panglez");

    TestPointRegionProfiles(0, 0, 0, "Ix", "Iy", "Iz");
    TestPointRegionProfiles(0, 0, 1, "Qx", "Qy", "Qz");
    TestPointRegionProfiles(0, 0, 2, "Ux", "Uy", "Uz");
    TestPointRegionProfiles(0, 0, 3, "Vx", "Vy", "Vz");

    TestPointRegionProfiles(0, 0, COMPUTE_STOKES_PTOTAL, "Ptotalx", "Ptotaly", "z");
    TestPointRegionProfiles(0, 0, COMPUTE_STOKES_PFTOTAL, "PFtotalx", "PFtotaly", "z");
    TestPointRegionProfiles(0, 0, COMPUTE_STOKES_PLINEAR, "Plinearx", "Plineary", "z");
    TestPointRegionProfiles(0, 0, COMPUTE_STOKES_PFLINEAR, "PFlinearx", "PFlineary", "z");
    TestPointRegionProfiles(0, 0, COMPUTE_STOKES_PANGLE, "Panglex", "Pangley", "z");

    TestPointRegionProfiles(0, 0, 0, "Ix", "Iy", "z");
    TestPointRegionProfiles(0, 0, 1, "Qx", "Qy", "z");
    TestPointRegionProfiles(0, 0, 2, "Ux", "Uy", "z");
    TestPointRegionProfiles(0, 0, 3, "Vx", "Vy", "z");
}

TEST_F(PolarizationCalculatorTest, TestCubeHistogram) {
    for (const auto& stokes : COMPUTED_STOKES) {
        TestCubeHistogram(stokes);
    }
}
