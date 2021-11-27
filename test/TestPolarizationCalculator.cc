/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "CommonTestUtilities.h"
#include "Frame.h"
#include "ImageData/PolarizationCalculator.h"
#include "Region/RegionHandler.h"
#include "Session.h"

static const string SAMPLE_IMAGE_CASA = "IRCp10216_sci.spw0.cube.IQUV.manual.pbcor.image";
static const string SAMPLE_IMAGE_FITS = "IRCp10216_sci.spw0.cube.IQUV.manual.pbcor.fits";
static const string SAMPLE_IMAGE_HDF5 = "IRCp10216_sci.spw0.cube.IQUV.manual.pbcor.hdf5";

static const int MAX_CHANNEL = 5;

using namespace carta;

class PolarizationCalculatorTest : public ::testing::Test, public FileFinder {
public:
    class TestFrame : public Frame {
    public:
        TestFrame(uint32_t session_id, std::shared_ptr<carta::FileLoader> loader, const std::string& hdu, int default_z = DEFAULT_Z)
            : Frame(session_id, loader, hdu, default_z) {}

        static void TestFrameImageCache(std::string sample_file_path) {
            std::shared_ptr<casacore::ImageInterface<float>> image;
            if (!OpenImage(image, sample_file_path)) {
                return;
            }

            // Get spectral axis size
            casacore::CoordinateSystem coord_sys = image->coordinates();
            int spectral_axis = coord_sys.spectralAxisNumber();
            int spectral_axis_size = image->shape()[spectral_axis];

            // Open the file through the Frame
            LoaderCache loaders(LOADER_CACHE_SIZE);
            std::unique_ptr<TestFrame> frame(new TestFrame(0, loaders.Get(sample_file_path), "0"));
            EXPECT_TRUE(frame->IsValid());
            EXPECT_TRUE(frame->_open_image_error.empty());

            // Set stokes to be calculated
            std::string message;
            int max_channel = (spectral_axis_size > MAX_CHANNEL) ? MAX_CHANNEL : spectral_axis_size;

            for (int channel = 0; channel < max_channel; ++channel) {
                frame->SetImageChannels(channel, COMPUTE_STOKES_PTOTAL, message);
                CheckFrameImageCache(image, channel, COMPUTE_STOKES_PTOTAL, frame->_image_cache);

                frame->SetImageChannels(channel, COMPUTE_STOKES_PFTOTAL, message);
                CheckFrameImageCache(image, channel, COMPUTE_STOKES_PFTOTAL, frame->_image_cache);

                frame->SetImageChannels(channel, COMPUTE_STOKES_PLINEAR, message);
                CheckFrameImageCache(image, channel, COMPUTE_STOKES_PLINEAR, frame->_image_cache);

                frame->SetImageChannels(channel, COMPUTE_STOKES_PFLINEAR, message);
                CheckFrameImageCache(image, channel, COMPUTE_STOKES_PFLINEAR, frame->_image_cache);

                frame->SetImageChannels(channel, COMPUTE_STOKES_PANGLE, message);
                CheckFrameImageCache(image, channel, COMPUTE_STOKES_PANGLE, frame->_image_cache);
            }
        }
    };

    static void CheckFrameImageCache(
        const std::shared_ptr<casacore::ImageInterface<float>>& image, int channel, int stokes, std::vector<float> data) {
        // Get spectral axis size
        casacore::CoordinateSystem coord_sys = image->coordinates();
        int spectral_axis = coord_sys.spectralAxisNumber();
        int spectral_axis_size = image->shape()[spectral_axis];

        // Verify each pixel value from calculation results
        int stokes_i(0);
        int stokes_q(1);
        int stokes_u(2);
        int stokes_v(3);
        std::vector<float> data_i;
        std::vector<float> data_q;
        std::vector<float> data_u;
        std::vector<float> data_v;
        GetImageData(image, AxisRange(channel), stokes_i, data_i);
        GetImageData(image, AxisRange(channel), stokes_q, data_q);
        GetImageData(image, AxisRange(channel), stokes_u, data_u);
        GetImageData(image, AxisRange(channel), stokes_v, data_v);

        EXPECT_EQ(data.size(), data_i.size());
        EXPECT_EQ(data.size(), data_q.size());
        EXPECT_EQ(data.size(), data_u.size());
        EXPECT_EQ(data.size(), data_v.size());

        for (int i = 0; i < data.size(); ++i) {
            if (!isnan(data[i])) {
                if (stokes == COMPUTE_STOKES_PTOTAL) {
                    auto total_polarized_intensity = sqrt(pow(data_q[i], 2) + pow(data_u[i], 2) + pow(data_v[i], 2));
                    EXPECT_FLOAT_EQ(data[i], total_polarized_intensity);
                } else if (stokes == COMPUTE_STOKES_PFTOTAL) {
                    auto total_fractional_polarized_intensity = sqrt(pow(data_q[i], 2) + pow(data_u[i], 2) + pow(data_v[i], 2)) / data_i[i];
                    EXPECT_FLOAT_EQ(data[i], total_fractional_polarized_intensity);
                } else if (stokes == COMPUTE_STOKES_PLINEAR) {
                    auto polarized_intensity = sqrt(pow(data_q[i], 2) + pow(data_u[i], 2));
                    EXPECT_FLOAT_EQ(data[i], polarized_intensity);
                } else if (stokes == COMPUTE_STOKES_PFLINEAR) {
                    auto fractional_polarized_intensity = sqrt(pow(data_q[i], 2) + pow(data_u[i], 2)) / data_i[i];
                    EXPECT_FLOAT_EQ(data[i], fractional_polarized_intensity);
                } else if (stokes == COMPUTE_STOKES_PANGLE) {
                    auto polarized_angle = atan2(data_u[i], data_q[i]) / 2;
                    EXPECT_FLOAT_EQ(data[i], polarized_angle);
                }
            }
        }
    }

    static void GetImageData(std::shared_ptr<const casacore::ImageInterface<casacore::Float>> image, AxisRange channel_axis_range,
        int stokes, std::vector<float>& data, AxisRange x_range = AxisRange(ALL_X), AxisRange y_range = AxisRange(ALL_Y)) {
        // Get spectral and stokes indices
        casacore::CoordinateSystem coord_sys = image->coordinates();
        casacore::Vector<casacore::Int> linear_axes = coord_sys.linearAxesNumbers();
        int spectral_axis = coord_sys.spectralAxisNumber();
        int stokes_axis = coord_sys.polarizationAxisNumber();

        // Get a slicer
        casacore::IPosition start(image->shape().size());
        start = 0;
        casacore::IPosition end(image->shape());
        end -= 1;

        auto x_axis_size = image->shape()[0];
        auto y_axis_size = image->shape()[1];
        auto spectral_axis_size = image->shape()[spectral_axis];

        // Set x range
        if ((x_range.from == ALL_X) && (x_range.to == ALL_X)) {
            start(0) = 0;
            end(0) = x_axis_size - 1;
        } else if ((x_range.from >= 0) && (x_range.from < x_axis_size) && (x_range.to >= 0) && (x_range.to < x_axis_size) &&
                   (x_range.from <= x_range.to)) {
            start(0) = x_range.from;
            end(0) = x_range.to;
        }

        // Set y range
        if ((y_range.from == ALL_Y) && (y_range.to == ALL_Y)) {
            start(1) = 0;
            end(1) = y_axis_size - 1;
        } else if ((y_range.from >= 0) && (y_range.from < y_axis_size) && (y_range.to >= 0) && (y_range.to < y_axis_size) &&
                   (y_range.from <= y_range.to)) {
            start(1) = y_range.from;
            end(1) = y_range.to;
        }

        // Check spectral axis range
        if (channel_axis_range.to == ALL_Z) {
            channel_axis_range.to = spectral_axis_size - 1;
        }
        if (channel_axis_range.from == ALL_Z) {
            channel_axis_range.from = 0;
        }
        if ((channel_axis_range.from > channel_axis_range.to) || (channel_axis_range.from < 0)) {
            spdlog::error("Invalid spectral axis range [{}, {}]", channel_axis_range.from, channel_axis_range.to);
            return;
        }
        if ((spectral_axis >= 0) && (channel_axis_range.to >= spectral_axis_size)) {
            spdlog::error(
                "channel number {} is greater or equal than the spectral axis size {}", channel_axis_range.to, spectral_axis_size);
            return;
        }

        if (spectral_axis >= 0) {
            start(spectral_axis) = channel_axis_range.from;
            end(spectral_axis) = channel_axis_range.to;
        }

        auto stokes_axis_size = image->shape()[stokes_axis];

        // Check stokes axis range
        if ((stokes_axis >= 0) && (stokes >= stokes_axis_size)) {
            spdlog::error("stokes number {} is greater or equal than the stokes axis size {}", stokes, stokes_axis_size);
            return;
        }

        if (stokes_axis >= 0) {
            start(stokes_axis) = stokes;
            end(stokes_axis) = stokes;
        }

        // Get image data
        casacore::Slicer section(start, end, casacore::Slicer::endIsLast);
        data.resize(section.length().product());
        casacore::Array<float> tmp(section.length(), data.data(), casacore::StorageInitPolicy::SHARE);
        casacore::SubImage<float> subimage(*image, section);
        casacore::RO_MaskedLatticeIterator<float> lattice_iter(subimage);

        for (lattice_iter.reset(); !lattice_iter.atEnd(); ++lattice_iter) {
            casacore::Array<float> cursor_data = lattice_iter.cursor();

            if (image->isMasked()) {
                casacore::Array<float> masked_data(cursor_data); // reference the same storage
                const casacore::Array<bool> cursor_mask = lattice_iter.getMask();

                // Apply cursor mask to cursor data: set masked values to NaN. booleans are used to delete copy of data if necessary
                bool del_mask_ptr;
                const bool* cursor_mask_ptr = cursor_mask.getStorage(del_mask_ptr);

                bool del_data_ptr;
                float* masked_data_ptr = masked_data.getStorage(del_data_ptr);

                for (size_t i = 0; i < cursor_data.nelements(); ++i) {
                    if (!cursor_mask_ptr[i]) {
                        masked_data_ptr[i] = NAN;
                    }
                }

                // free storage for cursor arrays
                cursor_mask.freeStorage(cursor_mask_ptr, del_mask_ptr);
                masked_data.putStorage(masked_data_ptr, del_data_ptr);
            }

            casacore::IPosition cursor_shape(lattice_iter.cursorShape());
            casacore::IPosition cursor_position(lattice_iter.position());
            casacore::Slicer cursor_slicer(cursor_position, cursor_shape); // where to put the data
            tmp(cursor_slicer) = cursor_data;
        }
    }

    static CARTA::SetSpectralRequirements_SpectralConfig CursorSpectralConfig(const std::string& coordinate) {
        CARTA::SetSpectralRequirements_SpectralConfig spectral_config;
        spectral_config.set_coordinate(coordinate);
        spectral_config.add_stats_types(CARTA::StatsType::Sum);
        return spectral_config;
    }

    static CARTA::SetSpectralRequirements_SpectralConfig RegionSpectralConfig(const std::string& coordinate) {
        CARTA::SetSpectralRequirements_SpectralConfig spectral_config;
        spectral_config.set_coordinate(coordinate);
        spectral_config.add_stats_types(CARTA::StatsType::Mean);
        return spectral_config;
    }

    static std::pair<std::vector<float>, std::vector<float>> GetSpatialProfiles(
        const std::shared_ptr<casacore::ImageInterface<float>>& image, int channel, int stokes, int cursor_x, int cursor_y) {
        // Get spectral axis size
        int x_size = image->shape()[0];
        int y_size = image->shape()[1];

        // Verify each pixel value from calculation results
        int stokes_i(0);
        int stokes_q(1);
        int stokes_u(2);
        int stokes_v(3);
        std::vector<float> data_i;
        std::vector<float> data_q;
        std::vector<float> data_u;
        std::vector<float> data_v;
        GetImageData(image, AxisRange(channel), stokes_i, data_i);
        GetImageData(image, AxisRange(channel), stokes_q, data_q);
        GetImageData(image, AxisRange(channel), stokes_u, data_u);
        GetImageData(image, AxisRange(channel), stokes_v, data_v);

        std::vector<float> profile_x;
        for (int i = 0; i < data_i.size(); ++i) {
            if (((i / x_size) == cursor_y) && ((i % x_size) < x_size)) {
                if (stokes == COMPUTE_STOKES_PTOTAL) {
                    auto total_polarized_intensity = sqrt(pow(data_q[i], 2) + pow(data_u[i], 2) + pow(data_v[i], 2));
                    profile_x.push_back(total_polarized_intensity);
                } else if (stokes == COMPUTE_STOKES_PFTOTAL) {
                    auto total_fractional_polarized_intensity = sqrt(pow(data_q[i], 2) + pow(data_u[i], 2) + pow(data_v[i], 2)) / data_i[i];
                    profile_x.push_back(total_fractional_polarized_intensity);
                } else if (stokes == COMPUTE_STOKES_PLINEAR) {
                    auto polarized_intensity = sqrt(pow(data_q[i], 2) + pow(data_u[i], 2));
                    profile_x.push_back(polarized_intensity);
                } else if (stokes == COMPUTE_STOKES_PFLINEAR) {
                    auto fractional_polarized_intensity = sqrt(pow(data_q[i], 2) + pow(data_u[i], 2)) / data_i[i];
                    profile_x.push_back(fractional_polarized_intensity);
                } else if (stokes == COMPUTE_STOKES_PANGLE) {
                    auto polarized_angle = atan2(data_u[i], data_q[i]) / 2;
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

        std::vector<float> profile_y;
        for (int i = 0; i < data_i.size(); ++i) {
            if ((i % x_size) == cursor_x) {
                if (stokes == COMPUTE_STOKES_PTOTAL) {
                    auto total_polarized_intensity = sqrt(pow(data_q[i], 2) + pow(data_u[i], 2) + pow(data_v[i], 2));
                    profile_y.push_back(total_polarized_intensity);
                } else if (stokes == COMPUTE_STOKES_PFTOTAL) {
                    auto total_fractional_polarized_intensity = sqrt(pow(data_q[i], 2) + pow(data_u[i], 2) + pow(data_v[i], 2)) / data_i[i];
                    profile_y.push_back(total_fractional_polarized_intensity);
                } else if (stokes == COMPUTE_STOKES_PLINEAR) {
                    auto polarized_intensity = sqrt(pow(data_q[i], 2) + pow(data_u[i], 2));
                    profile_y.push_back(polarized_intensity);
                } else if (stokes == COMPUTE_STOKES_PFLINEAR) {
                    auto polarized_intensity = sqrt(pow(data_q[i], 2) + pow(data_u[i], 2)) / data_i[i];
                    profile_y.push_back(polarized_intensity);
                } else if (stokes == COMPUTE_STOKES_PANGLE) {
                    auto polarized_angle = atan2(data_u[i], data_q[i]) / 2;
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
        // Get spectral axis size
        int x_size = image->shape()[0];
        int y_size = image->shape()[1];

        // Verify each pixel value from calculation results
        int stokes_i(0);
        int stokes_q(1);
        int stokes_u(2);
        int stokes_v(3);
        std::vector<float> data_i;
        std::vector<float> data_q;
        std::vector<float> data_u;
        std::vector<float> data_v;
        GetImageData(image, z_range, stokes_i, data_i, AxisRange(cursor_x), AxisRange(cursor_y));
        GetImageData(image, z_range, stokes_q, data_q, AxisRange(cursor_x), AxisRange(cursor_y));
        GetImageData(image, z_range, stokes_u, data_u, AxisRange(cursor_x), AxisRange(cursor_y));
        GetImageData(image, z_range, stokes_v, data_v, AxisRange(cursor_x), AxisRange(cursor_y));

        std::vector<float> profile;
        for (int i = 0; i < data_i.size(); ++i) {
            if (stokes == COMPUTE_STOKES_PTOTAL) {
                auto total_polarized_intensity = sqrt(pow(data_q[i], 2) + pow(data_u[i], 2) + pow(data_v[i], 2));
                profile.push_back(total_polarized_intensity);
            } else if (stokes == COMPUTE_STOKES_PFTOTAL) {
                auto total_fractional_polarized_intensity = sqrt(pow(data_q[i], 2) + pow(data_u[i], 2) + pow(data_v[i], 2)) / data_i[i];
                profile.push_back(total_fractional_polarized_intensity);
            } else if (stokes == COMPUTE_STOKES_PLINEAR) {
                auto polarized_intensity = sqrt(pow(data_q[i], 2) + pow(data_u[i], 2));
                profile.push_back(polarized_intensity);
            } else if (stokes == COMPUTE_STOKES_PFLINEAR) {
                auto fractional_polarized_intensity = sqrt(pow(data_q[i], 2) + pow(data_u[i], 2)) / data_i[i];
                profile.push_back(fractional_polarized_intensity);
            } else if (stokes == COMPUTE_STOKES_PANGLE) {
                auto polarized_angle = atan2(data_u[i], data_q[i]) / 2;
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

    static std::vector<float> SpatialProfileValues(CARTA::SpatialProfile& profile) {
        std::string buffer = profile.raw_values_fp32();
        std::vector<float> values(buffer.size() / sizeof(float));
        memcpy(values.data(), buffer.data(), buffer.size());
        return values;
    }

    static std::vector<float> SpectralProfileValues(CARTA::SpectralProfile& profile) {
        std::string buffer = profile.raw_values_fp32();
        std::vector<float> values(buffer.size() / sizeof(float));
        memcpy(values.data(), buffer.data(), buffer.size());
        return values;
    }

    static std::vector<double> SpectralProfileDoubleValues(CARTA::SpectralProfile& profile) {
        std::string buffer = profile.raw_values_fp64();
        std::vector<double> values(buffer.size() / sizeof(double));
        memcpy(values.data(), buffer.data(), buffer.size());
        return values;
    }

    static void CompareData(
        std::vector<CARTA::SpatialProfileData> data_vec, std::pair<std::vector<float>, std::vector<float>> data_profiles) {
        for (auto& data : data_vec) {
            auto profiles_x = data.profiles(0);
            auto profiles_y = data.profiles(1);
            auto data_x = SpatialProfileValues(profiles_x);
            auto data_y = SpatialProfileValues(profiles_y);

            EXPECT_EQ(data_profiles.first.size(), data_x.size());
            if (data_profiles.first.size() == data_x.size()) {
                for (int i = 0; i < data_x.size(); ++i) {
                    if (!isnan(data_profiles.first[i]) && !isnan(data_x[i])) {
                        EXPECT_FLOAT_EQ(data_profiles.first[i], data_x[i]);
                    }
                }
            }

            EXPECT_EQ(data_profiles.second.size(), data_y.size());
            if (data_profiles.second.size() == data_y.size()) {
                for (int i = 0; i < data_y.size(); ++i) {
                    if (!isnan(data_profiles.second[i]) && !isnan(data_y[i])) {
                        EXPECT_FLOAT_EQ(data_profiles.second[i], data_y[i]);
                    }
                }
            }
        }
    }

    static void CompareData(const std::vector<float>& data1, const std::vector<float>& data2) {
        EXPECT_EQ(data1.size(), data2.size());
        if (data1.size() == data2.size()) {
            for (int i = 0; i < data1.size(); ++i) {
                if (!isnan(data1[i]) && !isnan(data2[i])) {
                    EXPECT_FLOAT_EQ(data1[i], data2[i]);
                }
            }
        }
    }

    static void TestCursorProfiles(std::string sample_file_path, int current_channel, int current_stokes, int config_stokes,
        std::string stokes_config_x, std::string stokes_config_y, std::string stokes_config_z) {
        std::string reference_file_path = CasaImagePath(SAMPLE_IMAGE_CASA);
        std::shared_ptr<casacore::ImageInterface<float>> image;
        if (!OpenImage(image, reference_file_path)) {
            return;
        }

        // Get directional axis size
        int x_size = image->shape()[0];
        int y_size = image->shape()[1];

        // Open the file through the Frame
        LoaderCache loaders(LOADER_CACHE_SIZE);
        std::unique_ptr<Frame> frame(new Frame(0, loaders.Get(sample_file_path), "0"));
        EXPECT_TRUE(frame->IsValid());

        // Set spatial profiles requirements
        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {
            Message::SpatialConfig(stokes_config_x), Message::SpatialConfig(stokes_config_y)};
        frame->SetSpatialRequirements(profiles);

        int cursor_x(x_size / 2);
        int cursor_y(y_size / 2);
        frame->SetCursor(cursor_x, cursor_y);

        std::string message;
        frame->SetImageChannels(current_channel, current_stokes, message);

        // Get spatial profiles from the Frame
        std::vector<CARTA::SpatialProfileData> data_vec;
        frame->FillSpatialProfileData(data_vec);

        EXPECT_EQ(data_vec.size(), 1);

        // Get spatial profiles in another way
        auto data_profiles = GetSpatialProfiles(image, current_channel, config_stokes, cursor_x, cursor_y);

        // Check the consistency of two ways
        CompareData(data_vec, data_profiles);

        // Set spectral configs for the cursor
        std::vector<CARTA::SetSpectralRequirements_SpectralConfig> spectral_configs{CursorSpectralConfig(stokes_config_z)};
        frame->SetSpectralRequirements(CURSOR_REGION_ID, spectral_configs);

        // Get cursor spectral profile data from the Frame
        CARTA::SpectralProfile spectral_profile;
        bool stokes_changed = (stokes_config_z == "z");

        frame->FillSpectralProfileData(
            [&](CARTA::SpectralProfileData profile_data) {
                if (profile_data.progress() >= 1.0) {
                    spectral_profile = profile_data.profiles(0);
                }
            },
            CURSOR_REGION_ID, stokes_changed);

        std::vector<float> spectral_profile_data_1 = SpectralProfileValues(spectral_profile);

        // Get spatial profiles by another way
        int stokes = stokes_config_z == "z" ? current_stokes : config_stokes;
        std::vector<float> spectral_profile_data_2 = GetCursorSpectralProfiles(image, AxisRange(ALL_Z), stokes, cursor_x, cursor_y);

        // Check the consistency of two ways
        CompareData(spectral_profile_data_1, spectral_profile_data_2);
    }

    static void TestPointRegionProfiles(std::string sample_file_path, int current_channel, int current_stokes, int config_stokes,
        std::string stokes_config_x, std::string stokes_config_y, std::string stokes_config_z) {
        std::string reference_file_path = CasaImagePath(SAMPLE_IMAGE_CASA);
        std::shared_ptr<casacore::ImageInterface<float>> image;
        if (!OpenImage(image, reference_file_path)) {
            return;
        }

        // Open a sample image through the Frame
        if (!fs::exists(sample_file_path)) {
            return;
        }

        int file_id(0);
        LoaderCache loaders(LOADER_CACHE_SIZE);
        auto frame = std::make_shared<Frame>(file_id, loaders.Get(sample_file_path), "0");
        EXPECT_TRUE(frame->IsValid());

        // Set image channels through the Frame
        std::string message;
        frame->SetImageChannels(current_channel, current_stokes, message);

        // Get the coordinate through the Frame
        casacore::CoordinateSystem* csys = frame->CoordinateSystem();

        // Create a region handler
        auto region_handler = std::make_unique<carta::RegionHandler>();

        // Set a point region state
        int region_id(1);
        float rotation(0);
        int x_size = image->shape()[0];
        int y_size = image->shape()[1];
        int cursor_x(x_size / 2);
        int cursor_y(y_size / 2);
        CARTA::Point cursor_point;
        cursor_point.set_x(cursor_x);
        cursor_point.set_y(cursor_y);
        std::vector<CARTA::Point> points = {cursor_point};

        RegionState region_state(file_id, CARTA::RegionType::POINT, points, rotation);

        bool success = region_handler->SetRegion(region_id, region_state, csys);
        EXPECT_TRUE(success);

        // Set spatial requirements for a point region
        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {
            Message::SpatialConfig(stokes_config_x), Message::SpatialConfig(stokes_config_y)};
        region_handler->SetSpatialRequirements(region_id, file_id, frame, profiles);

        // Get a point region spatial profiles
        std::vector<CARTA::SpatialProfileData> spatial_profile_data_vec;
        auto projected_file_ids = region_handler->GetProjectedFileIds(region_id);
        for (auto projected_file_id : projected_file_ids) {
            region_handler->FillSpatialProfileData(projected_file_id, region_id, spatial_profile_data_vec);
        }

        // Get a point region spatial profiles in another way
        auto data_profiles = GetSpatialProfiles(image, current_channel, config_stokes, cursor_x, cursor_y);

        // Compare data
        CompareData(spatial_profile_data_vec, data_profiles);

        // Set spectral configs for a point region
        std::vector<CARTA::SetSpectralRequirements_SpectralConfig> spectral_configs{CursorSpectralConfig(stokes_config_z)};
        region_handler->SetSpectralRequirements(region_id, file_id, frame, spectral_configs);

        // Get cursor spectral profile data from the RegionHandler
        CARTA::SpectralProfile spectral_profile;
        bool stokes_changed = (stokes_config_z == "z");

        region_handler->FillSpectralProfileData(
            [&](CARTA::SpectralProfileData profile_data) {
                if (profile_data.progress() >= 1.0) {
                    spectral_profile = profile_data.profiles(0);
                }
            },
            region_id, file_id, stokes_changed);

        std::vector<double> tmp = SpectralProfileDoubleValues(spectral_profile);
        // convert the double type vector to the float type vector
        std::vector<float> spectral_profile_data_1(tmp.begin(), tmp.end());

        // Get spectral profiles by another way
        int stokes = stokes_config_z == "z" ? current_stokes : config_stokes;
        std::vector<float> spectral_profile_data_2 = GetCursorSpectralProfiles(image, AxisRange(ALL_Z), stokes, cursor_x, cursor_y);

        // Check the consistency of two ways
        CompareData(spectral_profile_data_1, spectral_profile_data_2);
    }

    static void TestRectangleRegionProfiles(
        std::string sample_file_path, int current_channel, int current_stokes, const std::string& stokes_config_z) {
        // Open a reference image
        std::string reference_file_path = CasaImagePath(SAMPLE_IMAGE_CASA);
        std::shared_ptr<casacore::ImageInterface<float>> image;
        if (!OpenImage(image, reference_file_path)) {
            return;
        }

        // Open a sample image through the Frame
        if (!fs::exists(sample_file_path)) {
            return;
        }

        int file_id(0);
        LoaderCache loaders(LOADER_CACHE_SIZE);
        auto frame = std::make_shared<Frame>(file_id, loaders.Get(sample_file_path), "0");
        EXPECT_TRUE(frame->IsValid());

        // Set image channels through the Frame
        std::string message;
        frame->SetImageChannels(current_channel, current_stokes, message);

        // Get the coordinate through the Frame
        casacore::CoordinateSystem* csys = frame->CoordinateSystem();

        // Create a region handler
        auto region_handler = std::make_unique<carta::RegionHandler>();

        // Set a rectangle region state: // [(cx,cy), (width,height)], width/height > 0
        int region_id(1);
        float rotation(0);
        int x_size = image->shape()[0];
        int y_size = image->shape()[1];
        int center_x(x_size / 2);
        int center_y(y_size / 2);
        CARTA::Point center, width_height; // set a rectangle region which covers the full x-y pixels range
        center.set_x(center_x);
        center.set_y(center_y);
        width_height.set_x(x_size);
        width_height.set_y(y_size);
        std::vector<CARTA::Point> points = {center, width_height};

        RegionState region_state(file_id, CARTA::RegionType::RECTANGLE, points, rotation);

        bool success = region_handler->SetRegion(region_id, region_state, csys);
        EXPECT_TRUE(success);

        // Set spectral configs for a point region
        std::vector<CARTA::SetSpectralRequirements_SpectralConfig> spectral_configs{RegionSpectralConfig(stokes_config_z)};
        region_handler->SetSpectralRequirements(region_id, file_id, frame, spectral_configs);

        // Get cursor spectral profile data from the RegionHandler
        CARTA::SpectralProfile spectral_profile;
        bool stokes_changed = (stokes_config_z == "z");

        region_handler->FillSpectralProfileData(
            [&](CARTA::SpectralProfileData profile_data) {
                if (profile_data.progress() >= 1.0) {
                    spectral_profile = profile_data.profiles(0);
                }
            },
            region_id, file_id, stokes_changed);

        std::vector<double> spectral_profile_data_double = SpectralProfileDoubleValues(spectral_profile);
        // convert the double type vector to the float type vector
        std::vector<float> spectral_profile_data_float(spectral_profile_data_double.begin(), spectral_profile_data_double.end());

        // get spectral profile in another way
        carta::PolarizationCalculator polarization_calculator(image);
        std::shared_ptr<casacore::ImageInterface<float>> resulting_image;
        int fiddled_stokes_index(0);
        int stokes;

        if (stokes_config_z == "z") {
            resulting_image = image;
            stokes = current_stokes;
        } else if (stokes_config_z == "Iz") {
            resulting_image = image;
            stokes = 0;
        } else if (stokes_config_z == "Qz") {
            resulting_image = image;
            stokes = 1;
        } else if (stokes_config_z == "Uz") {
            resulting_image = image;
            stokes = 2;
        } else if (stokes_config_z == "Vz") {
            resulting_image = image;
            stokes = 3;
        } else if (stokes_config_z == "Ptotalz") {
            resulting_image = polarization_calculator.ComputeTotalPolarizedIntensity();
            stokes = fiddled_stokes_index;
        } else if (stokes_config_z == "PFtotalz") {
            resulting_image = polarization_calculator.ComputeTotalFractionalPolarizedIntensity();
            stokes = fiddled_stokes_index;
        } else if (stokes_config_z == "Plinearz") {
            resulting_image = polarization_calculator.ComputePolarizedIntensity();
            stokes = fiddled_stokes_index;
        } else if (stokes_config_z == "PFlinearz") {
            resulting_image = polarization_calculator.ComputeFractionalPolarizedIntensity();
            stokes = fiddled_stokes_index;
        } else if (stokes_config_z == "Panglez") {
            resulting_image = polarization_calculator.ComputePolarizedAngle();
            stokes = fiddled_stokes_index;
        } else {
            spdlog::error("Unknown stokes coordinate config: {}", stokes_config_z);
        }

        int z_size = image->shape()[2];
        std::vector<double> spectral_profile_data_double_2;

        for (int channel = 0; channel < z_size; ++channel) {
            std::vector<float> tmp_data;
            GetImageData(resulting_image, AxisRange(channel), stokes, tmp_data);
            double tmp_sum(0);
            double tmp_count(0);
            for (int i = 0; i < tmp_data.size(); ++i) {
                if (!isnan(tmp_data[i])) {
                    tmp_sum += tmp_data[i];
                    ++tmp_count;
                }
            }
            double mean = tmp_sum / tmp_count;
            spectral_profile_data_double_2.push_back(mean);
        }

        // convert the double type vector to the float type vector
        std::vector<float> spectral_profile_data_float_2(spectral_profile_data_double_2.begin(), spectral_profile_data_double_2.end());

        // check the consistency of two ways
        CompareData(spectral_profile_data_float, spectral_profile_data_float_2);
    }

    static void Compare(const carta::Histogram& cube_histogram1, const carta::Histogram& cube_histogram2) {
        auto hist1 = cube_histogram1.GetHistogramBins();
        auto hist2 = cube_histogram2.GetHistogramBins();
        EXPECT_EQ(hist1.size(), hist2.size());
        if (hist1.size() == hist2.size()) {
            for (int i = 0; i < hist1.size(); ++i) {
                EXPECT_EQ(hist1[i], hist2[i]);
            }
        }
    }

    static void CalculateCubeHistogram(
        const std::shared_ptr<Frame>& frame, int current_channel, int current_stokes, carta::Histogram& cube_histogram) {
        // Set image channels
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

    static void TestCubeHistogram(std::string sample_file_path, int current_channel, int current_stokes) {
        // Open a reference image
        std::string reference_file_path = CasaImagePath(SAMPLE_IMAGE_CASA);
        std::shared_ptr<casacore::ImageInterface<float>> image;
        if (!OpenImage(image, reference_file_path)) {
            return;
        }

        // Open a sample image through the Frame
        if (!fs::exists(sample_file_path)) {
            return;
        }

        // Calculate the cube histogram
        LoaderCache loaders(LOADER_CACHE_SIZE);
        auto frame = std::make_shared<Frame>(0, loaders.Get(sample_file_path), "0");
        carta::Histogram cube_histogram;
        CalculateCubeHistogram(frame, current_channel, current_stokes, cube_histogram);

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
        int fiddled_stokes(0);

        carta::Histogram cube_histogram2;
        CalculateCubeHistogram(frame2, current_channel, fiddled_stokes, cube_histogram2);

        Compare(cube_histogram, cube_histogram2);
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

    EXPECT_TRUE(stokes_source_10.UseDefaultImage());
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
    TestFrame::TestFrameImageCache(CasaImagePath(SAMPLE_IMAGE_CASA));
    TestFrame::TestFrameImageCache(FitsImagePath(SAMPLE_IMAGE_FITS));
    TestFrame::TestFrameImageCache(Hdf5ImagePath(SAMPLE_IMAGE_HDF5));
}

TEST_F(PolarizationCalculatorTest, TestCursorProfiles) {
    TestCursorProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 1, 0, COMPUTE_STOKES_PTOTAL, "Ptotalx", "Ptotaly", "Ptotalz");
    TestCursorProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 1, 0, COMPUTE_STOKES_PFTOTAL, "PFtotalx", "PFtotaly", "PFtotalz");
    TestCursorProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 1, 0, COMPUTE_STOKES_PLINEAR, "Plinearx", "Plineary", "Plinearz");
    TestCursorProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 1, 0, COMPUTE_STOKES_PFLINEAR, "PFlinearx", "PFlineary", "PFlinearz");
    TestCursorProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 1, 0, COMPUTE_STOKES_PANGLE, "Panglex", "Pangley", "Panglez");

    TestCursorProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 1, 0, 0, "Ix", "Iy", "Iz");
    TestCursorProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 1, 0, 1, "Qx", "Qy", "Qz");
    TestCursorProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 1, 0, 2, "Ux", "Uy", "Uz");
    TestCursorProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 1, 0, 3, "Vx", "Vy", "Vz");

    TestCursorProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 1, 0, COMPUTE_STOKES_PTOTAL, "Ptotalx", "Ptotaly", "z");
    TestCursorProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 1, 0, COMPUTE_STOKES_PFTOTAL, "PFtotalx", "PFtotaly", "z");
    TestCursorProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 1, 0, COMPUTE_STOKES_PLINEAR, "Plinearx", "Plineary", "z");
    TestCursorProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 1, 0, COMPUTE_STOKES_PFLINEAR, "PFlinearx", "PFlineary", "z");
    TestCursorProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 1, 0, COMPUTE_STOKES_PANGLE, "Panglex", "Pangley", "z");

    TestCursorProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 1, 0, 0, "Ix", "Iy", "z");
    TestCursorProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 1, 0, 1, "Qx", "Qy", "z");
    TestCursorProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 1, 0, 2, "Ux", "Uy", "z");
    TestCursorProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 1, 0, 3, "Vx", "Vy", "z");

    TestCursorProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 1, 0, COMPUTE_STOKES_PTOTAL, "Ptotalx", "Ptotaly", "Ptotalz");
    TestCursorProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 1, 0, COMPUTE_STOKES_PFTOTAL, "PFtotalx", "PFtotaly", "PFtotalz");
    TestCursorProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 1, 0, COMPUTE_STOKES_PLINEAR, "Plinearx", "Plineary", "Plinearz");
    TestCursorProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 1, 0, COMPUTE_STOKES_PFLINEAR, "PFlinearx", "PFlineary", "PFlinearz");
    TestCursorProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 1, 0, COMPUTE_STOKES_PANGLE, "Panglex", "Pangley", "Panglez");

    TestCursorProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 1, 0, 0, "Ix", "Iy", "Iz");
    TestCursorProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 1, 0, 1, "Qx", "Qy", "Qz");
    TestCursorProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 1, 0, 2, "Ux", "Uy", "Uz");
    TestCursorProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 1, 0, 3, "Vx", "Vy", "Vz");

    TestCursorProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 1, 0, COMPUTE_STOKES_PTOTAL, "Ptotalx", "Ptotaly", "z");
    TestCursorProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 1, 0, COMPUTE_STOKES_PFTOTAL, "PFtotalx", "PFtotaly", "z");
    TestCursorProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 1, 0, COMPUTE_STOKES_PLINEAR, "Plinearx", "Plineary", "z");
    TestCursorProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 1, 0, COMPUTE_STOKES_PFLINEAR, "PFlinearx", "PFlineary", "z");
    TestCursorProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 1, 0, COMPUTE_STOKES_PANGLE, "Panglex", "Pangley", "z");

    TestCursorProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 1, 0, 0, "Ix", "Iy", "z");
    TestCursorProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 1, 0, 1, "Qx", "Qy", "z");
    TestCursorProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 1, 0, 2, "Ux", "Uy", "z");
    TestCursorProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 1, 0, 3, "Vx", "Vy", "z");
}

TEST_F(PolarizationCalculatorTest, TestPointRegionProfiles) {
    TestPointRegionProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, COMPUTE_STOKES_PTOTAL, "Ptotalx", "Ptotaly", "Ptotalz");
    TestPointRegionProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, COMPUTE_STOKES_PFTOTAL, "PFtotalx", "PFtotaly", "PFtotalz");
    TestPointRegionProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, COMPUTE_STOKES_PLINEAR, "Plinearx", "Plineary", "Plinearz");
    TestPointRegionProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, COMPUTE_STOKES_PFLINEAR, "PFlinearx", "PFlineary", "PFlinearz");
    TestPointRegionProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, COMPUTE_STOKES_PANGLE, "Panglex", "Pangley", "Panglez");

    TestPointRegionProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, COMPUTE_STOKES_PTOTAL, "Ptotalx", "Ptotaly", "Ptotalz");
    TestPointRegionProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, COMPUTE_STOKES_PFTOTAL, "PFtotalx", "PFtotaly", "PFtotalz");
    TestPointRegionProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, COMPUTE_STOKES_PLINEAR, "Plinearx", "Plineary", "Plinearz");
    TestPointRegionProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, COMPUTE_STOKES_PFLINEAR, "PFlinearx", "PFlineary", "PFlinearz");
    TestPointRegionProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, COMPUTE_STOKES_PANGLE, "Panglex", "Pangley", "Panglez");

    TestPointRegionProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, 0, "Ix", "Iy", "Iz");
    TestPointRegionProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, 1, "Qx", "Qy", "Qz");
    TestPointRegionProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, 2, "Ux", "Uy", "Uz");
    TestPointRegionProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, 3, "Vx", "Vy", "Vz");

    TestPointRegionProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, 0, "Ix", "Iy", "Iz");
    TestPointRegionProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, 1, "Qx", "Qy", "Qz");
    TestPointRegionProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, 2, "Ux", "Uy", "Uz");
    TestPointRegionProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, 3, "Vx", "Vy", "Vz");

    TestPointRegionProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, COMPUTE_STOKES_PTOTAL, "Ptotalx", "Ptotaly", "z");
    TestPointRegionProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, COMPUTE_STOKES_PFTOTAL, "PFtotalx", "PFtotaly", "z");
    TestPointRegionProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, COMPUTE_STOKES_PLINEAR, "Plinearx", "Plineary", "z");
    TestPointRegionProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, COMPUTE_STOKES_PFLINEAR, "PFlinearx", "PFlineary", "z");
    TestPointRegionProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, COMPUTE_STOKES_PANGLE, "Panglex", "Pangley", "z");

    TestPointRegionProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, COMPUTE_STOKES_PTOTAL, "Ptotalx", "Ptotaly", "z");
    TestPointRegionProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, COMPUTE_STOKES_PFTOTAL, "PFtotalx", "PFtotaly", "z");
    TestPointRegionProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, COMPUTE_STOKES_PLINEAR, "Plinearx", "Plineary", "z");
    TestPointRegionProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, COMPUTE_STOKES_PFLINEAR, "PFlinearx", "PFlineary", "z");
    TestPointRegionProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, COMPUTE_STOKES_PANGLE, "Panglex", "Pangley", "z");

    TestPointRegionProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, 0, "Ix", "Iy", "z");
    TestPointRegionProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, 1, "Qx", "Qy", "z");
    TestPointRegionProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, 2, "Ux", "Uy", "z");
    TestPointRegionProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, 3, "Vx", "Vy", "z");

    TestPointRegionProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, 0, "Ix", "Iy", "z");
    TestPointRegionProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, 1, "Qx", "Qy", "z");
    TestPointRegionProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, 2, "Ux", "Uy", "z");
    TestPointRegionProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, 3, "Vx", "Vy", "z");
}

TEST_F(PolarizationCalculatorTest, TestRectangleRegionProfiles) {
    TestRectangleRegionProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, "Ptotalz");
    TestRectangleRegionProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, "PFtotalz");
    TestRectangleRegionProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, "Plinearz");
    TestRectangleRegionProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, "PFlinearz");
    TestRectangleRegionProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, "Panglez");

    TestRectangleRegionProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, "Iz");
    TestRectangleRegionProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, "Qz");
    TestRectangleRegionProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, "Uz");
    TestRectangleRegionProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, "Vz");

    TestRectangleRegionProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, "z");
    TestRectangleRegionProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 0, 1, "z");
    TestRectangleRegionProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 0, 2, "z");
    TestRectangleRegionProfiles(FitsImagePath(SAMPLE_IMAGE_FITS), 0, 3, "z");

    TestRectangleRegionProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, "Ptotalz");
    TestRectangleRegionProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, "PFtotalz");
    TestRectangleRegionProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, "Plinearz");
    TestRectangleRegionProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, "PFlinearz");
    TestRectangleRegionProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, "Panglez");

    TestRectangleRegionProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, "Iz");
    TestRectangleRegionProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, "Qz");
    TestRectangleRegionProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, "Uz");
    TestRectangleRegionProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, "Vz");

    TestRectangleRegionProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, "z");
    TestRectangleRegionProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 1, "z");
    TestRectangleRegionProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 2, "z");
    TestRectangleRegionProfiles(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 3, "z");
}

TEST_F(PolarizationCalculatorTest, TestCubeHistogram) {
    TestCubeHistogram(FitsImagePath(SAMPLE_IMAGE_FITS), 0, COMPUTE_STOKES_PTOTAL);
    TestCubeHistogram(Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, COMPUTE_STOKES_PTOTAL);
}
