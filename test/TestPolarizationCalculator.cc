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

class PolarizationCalculatorTest : public ::testing::Test, public FileFinder {
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
            std::vector<float> data_vec;
            for (int channel = 0; channel < spectral_axis_size; ++channel) {
                frame->SetImageChannels(channel, COMPUTE_STOKES_PTOTAL, message);
                GetDataVector(data_vec, frame->_image_cache.get(), frame->_image_cache_size);
                CheckFrameImageCache(image, channel, COMPUTE_STOKES_PTOTAL, data_vec);

                frame->SetImageChannels(channel, COMPUTE_STOKES_PFTOTAL, message);
                GetDataVector(data_vec, frame->_image_cache.get(), frame->_image_cache_size);
                CheckFrameImageCache(image, channel, COMPUTE_STOKES_PFTOTAL, data_vec);

                frame->SetImageChannels(channel, COMPUTE_STOKES_PLINEAR, message);
                GetDataVector(data_vec, frame->_image_cache.get(), frame->_image_cache_size);
                CheckFrameImageCache(image, channel, COMPUTE_STOKES_PLINEAR, data_vec);

                frame->SetImageChannels(channel, COMPUTE_STOKES_PFLINEAR, message);
                GetDataVector(data_vec, frame->_image_cache.get(), frame->_image_cache_size);
                CheckFrameImageCache(image, channel, COMPUTE_STOKES_PFLINEAR, data_vec);

                frame->SetImageChannels(channel, COMPUTE_STOKES_PANGLE, message);
                GetDataVector(data_vec, frame->_image_cache.get(), frame->_image_cache_size);
                CheckFrameImageCache(image, channel, COMPUTE_STOKES_PANGLE, data_vec);
            }
        }

        static void GetDataVector(std::vector<float>& data_vec, float* data_ptr, size_t data_size) {
            data_vec.resize(data_size);
            for (int i = 0; i < data_size; ++i) {
                data_vec[i] = data_ptr[i];
            }
        }
    };

    static void CheckFrameImageCache(
        const std::shared_ptr<casacore::ImageInterface<float>>& image, int channel, int stokes, const std::vector<float>& data) {
        if (image->ndim() < 4) {
            spdlog::error("Invalid image dimension.");
            return;
        }

        // Assume stokes indices, I = 0, Q = 1, U = 2, V = 3
        std::vector<float> data_i, data_q, data_u, data_v;
        GetImageData(data_i, image, 0, AxisRange(channel));
        GetImageData(data_q, image, 1, AxisRange(channel));
        GetImageData(data_u, image, 2, AxisRange(channel));
        GetImageData(data_v, image, 3, AxisRange(channel));

        EXPECT_TRUE((data.size() == data_i.size()) && (data.size() == data_q.size()) && (data.size() == data_u.size()) &&
                    (data.size() == data_v.size()));

        // Verify each pixel value from calculation results
        for (int i = 0; i < data.size(); ++i) {
            if (!isnan(data[i])) {
                if (stokes == COMPUTE_STOKES_PTOTAL) {
                    auto total_polarized_intensity = sqrt(pow(data_q[i], 2) + pow(data_u[i], 2) + pow(data_v[i], 2));
                    EXPECT_FLOAT_EQ(data[i], total_polarized_intensity);
                } else if (stokes == COMPUTE_STOKES_PFTOTAL) {
                    auto total_fractional_polarized_intensity =
                        100.0 * sqrt(pow(data_q[i], 2) + pow(data_u[i], 2) + pow(data_v[i], 2)) / data_i[i];
                    EXPECT_FLOAT_EQ(data[i], total_fractional_polarized_intensity);
                } else if (stokes == COMPUTE_STOKES_PLINEAR) {
                    auto polarized_intensity = sqrt(pow(data_q[i], 2) + pow(data_u[i], 2));
                    EXPECT_FLOAT_EQ(data[i], polarized_intensity);
                } else if (stokes == COMPUTE_STOKES_PFLINEAR) {
                    auto fractional_polarized_intensity = 100.0 * sqrt(pow(data_q[i], 2) + pow(data_u[i], 2)) / data_i[i];
                    EXPECT_FLOAT_EQ(data[i], fractional_polarized_intensity);
                } else if (stokes == COMPUTE_STOKES_PANGLE) {
                    auto polarized_angle = (180.0 / casacore::C::pi) * atan2(data_u[i], data_q[i]) / 2;
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
        int y_size = image->shape()[1];

        // Assume stokes indices, I = 0, Q = 1, U = 2, V = 3
        std::vector<float> data_i, data_q, data_u, data_v;
        GetImageData(data_i, image, 0, AxisRange(channel));
        GetImageData(data_q, image, 1, AxisRange(channel));
        GetImageData(data_u, image, 2, AxisRange(channel));
        GetImageData(data_v, image, 3, AxisRange(channel));

        // Get profile x
        std::vector<float> profile_x;
        for (int i = 0; i < data_i.size(); ++i) {
            if (((i / x_size) == cursor_y) && ((i % x_size) < x_size)) {
                if (stokes == COMPUTE_STOKES_PTOTAL) {
                    auto total_polarized_intensity = sqrt(pow(data_q[i], 2) + pow(data_u[i], 2) + pow(data_v[i], 2));
                    profile_x.push_back(total_polarized_intensity);
                } else if (stokes == COMPUTE_STOKES_PFTOTAL) {
                    auto total_fractional_polarized_intensity =
                        100.0 * sqrt(pow(data_q[i], 2) + pow(data_u[i], 2) + pow(data_v[i], 2)) / data_i[i];
                    profile_x.push_back(total_fractional_polarized_intensity);
                } else if (stokes == COMPUTE_STOKES_PLINEAR) {
                    auto polarized_intensity = sqrt(pow(data_q[i], 2) + pow(data_u[i], 2));
                    profile_x.push_back(polarized_intensity);
                } else if (stokes == COMPUTE_STOKES_PFLINEAR) {
                    auto fractional_polarized_intensity = 100.0 * sqrt(pow(data_q[i], 2) + pow(data_u[i], 2)) / data_i[i];
                    profile_x.push_back(fractional_polarized_intensity);
                } else if (stokes == COMPUTE_STOKES_PANGLE) {
                    auto polarized_angle = (180.0 / casacore::C::pi) * atan2(data_u[i], data_q[i]) / 2;
                    profile_x.push_back(polarized_angle);
                } else if (stokes == 0) {
                    profile_x.push_back(data_i[i]);
                } else if (stokes == 1) {
                    profile_x.push_back(data_q[i]);
                } else if (stokes == 2) {
                    profile_x.push_back(data_u[i]);
                } else if (stokes == 3) {
                    profile_x.push_back(data_v[i]);
                } else {
                    spdlog::error("Unknown stokes: {}", stokes);
                }
            }
        }

        // Get profile y
        std::vector<float> profile_y;
        for (int i = 0; i < data_i.size(); ++i) {
            if ((i % x_size) == cursor_x) {
                if (stokes == COMPUTE_STOKES_PTOTAL) {
                    auto total_polarized_intensity = sqrt(pow(data_q[i], 2) + pow(data_u[i], 2) + pow(data_v[i], 2));
                    profile_y.push_back(total_polarized_intensity);
                } else if (stokes == COMPUTE_STOKES_PFTOTAL) {
                    auto total_fractional_polarized_intensity =
                        100.0 * sqrt(pow(data_q[i], 2) + pow(data_u[i], 2) + pow(data_v[i], 2)) / data_i[i];
                    profile_y.push_back(total_fractional_polarized_intensity);
                } else if (stokes == COMPUTE_STOKES_PLINEAR) {
                    auto polarized_intensity = sqrt(pow(data_q[i], 2) + pow(data_u[i], 2));
                    profile_y.push_back(polarized_intensity);
                } else if (stokes == COMPUTE_STOKES_PFLINEAR) {
                    auto polarized_intensity = 100.0 * sqrt(pow(data_q[i], 2) + pow(data_u[i], 2)) / data_i[i];
                    profile_y.push_back(polarized_intensity);
                } else if (stokes == COMPUTE_STOKES_PANGLE) {
                    auto polarized_angle = (180.0 / casacore::C::pi) * atan2(data_u[i], data_q[i]) / 2;
                    profile_y.push_back(polarized_angle);
                } else if (stokes == 0) {
                    profile_y.push_back(data_i[i]);
                } else if (stokes == 1) {
                    profile_y.push_back(data_q[i]);
                } else if (stokes == 2) {
                    profile_y.push_back(data_u[i]);
                } else if (stokes == 3) {
                    profile_y.push_back(data_v[i]);
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

        // Assume stokes indices, I = 0, Q = 1, U = 2, V = 3
        std::vector<float> data_i, data_q, data_u, data_v;
        GetImageData(data_i, image, 0, z_range, AxisRange(cursor_x), AxisRange(cursor_y));
        GetImageData(data_q, image, 1, z_range, AxisRange(cursor_x), AxisRange(cursor_y));
        GetImageData(data_u, image, 2, z_range, AxisRange(cursor_x), AxisRange(cursor_y));
        GetImageData(data_v, image, 3, z_range, AxisRange(cursor_x), AxisRange(cursor_y));

        // Get profile z
        std::vector<float> profile;
        for (int i = 0; i < data_i.size(); ++i) {
            if (stokes == COMPUTE_STOKES_PTOTAL) {
                auto total_polarized_intensity = sqrt(pow(data_q[i], 2) + pow(data_u[i], 2) + pow(data_v[i], 2));
                profile.push_back(total_polarized_intensity);
            } else if (stokes == COMPUTE_STOKES_PFTOTAL) {
                auto total_fractional_polarized_intensity =
                    100.0 * sqrt(pow(data_q[i], 2) + pow(data_u[i], 2) + pow(data_v[i], 2)) / data_i[i];
                profile.push_back(total_fractional_polarized_intensity);
            } else if (stokes == COMPUTE_STOKES_PLINEAR) {
                auto polarized_intensity = sqrt(pow(data_q[i], 2) + pow(data_u[i], 2));
                profile.push_back(polarized_intensity);
            } else if (stokes == COMPUTE_STOKES_PFLINEAR) {
                auto fractional_polarized_intensity = 100.0 * sqrt(pow(data_q[i], 2) + pow(data_u[i], 2)) / data_i[i];
                profile.push_back(fractional_polarized_intensity);
            } else if (stokes == COMPUTE_STOKES_PANGLE) {
                auto polarized_angle = (180.0 / casacore::C::pi) * atan2(data_u[i], data_q[i]) / 2;
                profile.push_back(polarized_angle);
            } else if (stokes == 0) {
                profile.push_back(data_i[i]);
            } else if (stokes == 1) {
                profile.push_back(data_q[i]);
            } else if (stokes == 2) {
                profile.push_back(data_u[i]);
            } else if (stokes == 3) {
                profile.push_back(data_v[i]);
            } else {
                spdlog::error("Unknown stokes: {}", stokes);
            }
        }
        return profile;
    }

    static void TestCursorProfiles(int current_channel, int current_stokes, int config_stokes, std::string stokes_config_x,
        std::string stokes_config_y, std::string stokes_config_z) {
        auto ref_file_path = ImageGenerator::GeneratedFitsImagePath(IMAGE_SHAPE, IMAGE_OPTS);
        auto exp_file_path = ImageGenerator::GeneratedHdf5ImagePath(IMAGE_SHAPE, IMAGE_OPTS);

        // Open an image file
        std::shared_ptr<casacore::ImageInterface<float>> image;
        EXPECT_TRUE(OpenImage(image, ref_file_path));

        // Open an image file through Frame
        LoaderCache loaders(LOADER_CACHE_SIZE);
        std::unique_ptr<Frame> frame(new Frame(0, loaders.Get(exp_file_path), "0"));
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
        std::vector<CARTA::SpatialProfileData> data_vec;
        frame->FillSpatialProfileData(data_vec);

        // Get spatial spatial_configs in another way
        auto data_profiles = GetCursorSpatialProfiles(image, current_channel, config_stokes, cursor_x, cursor_y);

        // Check the consistency of two ways
        CmpSpatialProfiles(data_vec, data_profiles);

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

        // Get spatial spatial_configs by another way
        int stokes = (stokes_config_z == "z") ? current_stokes : config_stokes;
        std::vector<float> spectral_profile_as_float2 = GetCursorSpectralProfiles(image, AxisRange(ALL_Z), stokes, cursor_x, cursor_y);

        // Check the consistency of two ways
        CmpVectors(spectral_profile_as_float1, spectral_profile_as_float2);
    }

    static void TestPointRegionProfiles(int current_channel, int current_stokes, int config_stokes, std::string stokes_config_x,
        std::string stokes_config_y, std::string stokes_config_z) {
        auto ref_file_path = ImageGenerator::GeneratedFitsImagePath(IMAGE_SHAPE, IMAGE_OPTS);
        auto exp_file_path = ImageGenerator::GeneratedHdf5ImagePath(IMAGE_SHAPE, IMAGE_OPTS);

        // Open a reference image file
        std::shared_ptr<casacore::ImageInterface<float>> image;
        EXPECT_TRUE(OpenImage(image, ref_file_path));

        // Open an experimental image through the Frame
        int file_id(0);
        LoaderCache loaders(LOADER_CACHE_SIZE);
        auto frame = std::make_shared<Frame>(file_id, loaders.Get(exp_file_path), "0");
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
        auto projected_file_ids = region_handler->GetProjectedFileIds(region_id);
        for (auto projected_file_id : projected_file_ids) {
            region_handler->FillSpatialProfileData(projected_file_id, region_id, spatial_profiles1);
        }

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

        // Get spectral profiles by another way
        int stokes = (stokes_config_z == "z") ? current_stokes : config_stokes;
        std::vector<float> spectral_profile_as_float2 = GetCursorSpectralProfiles(image, AxisRange(ALL_Z), stokes, cursor_x, cursor_y);

        // Check the consistency of two ways
        CmpVectors(spectral_profile_as_float1, spectral_profile_as_float2);
    }

    static void TestRectangleRegionProfiles(int current_channel, int current_stokes, const std::string& stokes_config_z) {
        auto ref_file_path = ImageGenerator::GeneratedFitsImagePath(IMAGE_SHAPE, IMAGE_OPTS);
        auto exp_file_path = ImageGenerator::GeneratedHdf5ImagePath(IMAGE_SHAPE, IMAGE_OPTS);

        // Open a reference image file
        std::shared_ptr<casacore::ImageInterface<float>> image;
        EXPECT_TRUE(OpenImage(image, ref_file_path));

        // Open an experimental image file through Frame
        int file_id(0);
        LoaderCache loaders(LOADER_CACHE_SIZE);
        auto frame = std::make_shared<Frame>(file_id, loaders.Get(exp_file_path), "0");
        EXPECT_TRUE(frame->IsValid());

        // Set image channels through the Frame
        std::string message;
        frame->SetImageChannels(current_channel, current_stokes, message);

        // Create a region handler
        auto region_handler = std::make_unique<carta::RegionHandler>();

        // Set a rectangle region state: // [(cx,cy), (width,height)], width/height > 0
        int region_id(1);
        int x_size = image->shape()[0];
        int y_size = image->shape()[1];
        int center_x(x_size / 2);
        int center_y(y_size / 2);
        std::vector<CARTA::Point> points = {Message::Point(center_x, center_y), Message::Point(x_size, y_size)}; //{center, width/height};

        RegionState region_state(file_id, CARTA::RegionType::RECTANGLE, points, 0);
        EXPECT_TRUE(region_handler->SetRegion(region_id, region_state, frame->CoordinateSystem()));

        // Set spectral configs for a rectangle region
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

        auto spectral_profile_as_double1 = GetSpectralProfileValues<double>(spectral_profile);
        // convert the double type vector to the float type vector
        std::vector<float> spectral_profile_as_float1(spectral_profile_as_double1.begin(), spectral_profile_as_double1.end());

        // get spectral profile in another way
        int stokes(-1);

        if (stokes_config_z == "z") {
            stokes = current_stokes;
        } else if (stokes_config_z == "Iz") {
            stokes = 0;
        } else if (stokes_config_z == "Qz") {
            stokes = 1;
        } else if (stokes_config_z == "Uz") {
            stokes = 2;
        } else if (stokes_config_z == "Vz") {
            stokes = 3;
        }

        int z_size = image->shape()[2];
        std::vector<double> spectral_profile_as_double2;

        for (int channel = 0; channel < z_size; ++channel) {
            if (stokes > -1) {
                // For regular stokes types
                std::vector<float> tmp_data;
                GetImageData(tmp_data, image, stokes, AxisRange(channel));
                double tmp_sum(0), tmp_count(0);
                for (int i = 0; i < tmp_data.size(); ++i) {
                    if (!isnan(tmp_data[i])) {
                        tmp_sum += tmp_data[i];
                        ++tmp_count;
                    }
                }
                double mean = tmp_sum / tmp_count;
                // Fill results
                spectral_profile_as_double2.push_back(mean);
            } else {
                // For computed stokes types
                double i = GetMeanOfStokesChannel(image, 0, channel);
                double q = GetMeanOfStokesChannel(image, 1, channel);
                double u = GetMeanOfStokesChannel(image, 2, channel);
                double v = GetMeanOfStokesChannel(image, 3, channel);
                double result = std::numeric_limits<double>::quiet_NaN();

                if (stokes_config_z == "Ptotalz") {
                    result = sqrt(pow(q, 2) + pow(u, 2) + pow(v, 2));
                } else if (stokes_config_z == "PFtotalz") {
                    result = 100.0 * sqrt(pow(q, 2) + pow(u, 2) + pow(v, 2)) / i;
                } else if (stokes_config_z == "Plinearz") {
                    result = sqrt(pow(q, 2) + pow(u, 2));
                } else if (stokes_config_z == "PFlinearz") {
                    result = 100.0 * sqrt(pow(q, 2) + pow(u, 2)) / i;
                } else if (stokes_config_z == "Panglez") {
                    result = (180.0 / casacore::C::pi) * atan2(u, q) / 2;
                }
                // Fill results
                spectral_profile_as_double2.push_back(result);
            }
        }

        // convert the double type vector to the float type vector
        std::vector<float> spectral_profile_as_float2(spectral_profile_as_double2.begin(), spectral_profile_as_double2.end());

        // check the consistency of two ways
        CmpVectors(spectral_profile_as_float1, spectral_profile_as_float2);
    }

    static double GetMeanOfStokesChannel(std::shared_ptr<casacore::ImageInterface<float>> image, int stokes, int channel) {
        std::vector<float> tmp_data;
        GetImageData(tmp_data, image, stokes, AxisRange(channel));
        double tmp_sum(0), tmp_count(0);
        for (int i = 0; i < tmp_data.size(); ++i) {
            if (!isnan(tmp_data[i])) {
                tmp_sum += tmp_data[i];
                ++tmp_count;
            }
        }
        return tmp_sum / tmp_count;
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
        auto ref_file_path = ImageGenerator::GeneratedFitsImagePath(IMAGE_SHAPE, IMAGE_OPTS);
        auto exp_file_path = ImageGenerator::GeneratedHdf5ImagePath(IMAGE_SHAPE, IMAGE_OPTS);

        // Open a reference image file
        std::shared_ptr<casacore::ImageInterface<float>> image;
        EXPECT_TRUE(OpenImage(image, ref_file_path));

        // Calculate the cube histogram
        LoaderCache loaders(LOADER_CACHE_SIZE);
        auto frame1 = std::make_shared<Frame>(0, loaders.Get(exp_file_path), "0");
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

TEST_F(PolarizationCalculatorTest, TestRectangleRegionProfiles) {
    TestRectangleRegionProfiles(0, 0, "Ptotalz");
    TestRectangleRegionProfiles(0, 0, "PFtotalz");
    TestRectangleRegionProfiles(0, 0, "Plinearz");
    TestRectangleRegionProfiles(0, 0, "PFlinearz");
    TestRectangleRegionProfiles(0, 0, "Panglez");

    TestRectangleRegionProfiles(0, 0, "Iz");
    TestRectangleRegionProfiles(0, 0, "Qz");
    TestRectangleRegionProfiles(0, 0, "Uz");
    TestRectangleRegionProfiles(0, 0, "Vz");
    TestRectangleRegionProfiles(0, 0, "z");
}

TEST_F(PolarizationCalculatorTest, TestCubeHistogram) {
    TestCubeHistogram(COMPUTE_STOKES_PTOTAL);
    TestCubeHistogram(COMPUTE_STOKES_PFTOTAL);
    TestCubeHistogram(COMPUTE_STOKES_PLINEAR);
    TestCubeHistogram(COMPUTE_STOKES_PFLINEAR);
    TestCubeHistogram(COMPUTE_STOKES_PANGLE);
}
