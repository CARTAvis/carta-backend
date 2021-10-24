/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include <casacore/images/Images/PagedImage.h>
#include <imageanalysis/ImageAnalysis/ImageMoments.h>

#include "CommonTestUtilities.h"
#include "Frame.h"
#include "ImageData/PolarizationCalculator.h"
#include "Logger/Logger.h"
#include "Region/RegionHandler.h"

static const string SAMPLE_IMAGE_CASA = "IRCp10216_sci.spw0.cube.IQUV.manual.pbcor.image";
static const string SAMPLE_IMAGE_FITS = "IRCp10216_sci.spw0.cube.IQUV.manual.pbcor.fits";
static const string SAMPLE_IMAGE_HDF5 = "IRCp10216_sci.spw0.cube.IQUV.manual.pbcor.hdf5";
static const int MAX_CHANNEL = 5;

using namespace carta;

class PolarizationCalculatorTest : public ::testing::Test, public FileFinder {
public:
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

                // Apply cursor mask to cursor data: set masked values to NaN.
                // booleans are used to delete copy of data if necessary
                bool del_mask_ptr;
                const bool* pCursorMask = cursor_mask.getStorage(del_mask_ptr);

                bool del_data_ptr;
                float* pMaskedData = masked_data.getStorage(del_data_ptr);

                for (size_t i = 0; i < cursor_data.nelements(); ++i) {
                    if (!pCursorMask[i]) {
                        pMaskedData[i] = NAN;
                    }
                }

                // free storage for cursor arrays
                cursor_mask.freeStorage(pCursorMask, del_mask_ptr);
                masked_data.putStorage(pMaskedData, del_data_ptr);
            }

            casacore::IPosition cursor_shape(lattice_iter.cursorShape());
            casacore::IPosition cursor_position(lattice_iter.position());
            casacore::Slicer cursor_slicer(cursor_position, cursor_shape); // where to put the data
            tmp(cursor_slicer) = cursor_data;
        }
    }

    static void CheckPolarizationType(
        const std::shared_ptr<casacore::ImageInterface<float>>& image, casacore::Stokes::StokesTypes expected_stokes_type) {
        casacore::CoordinateSystem coord_sys = image->coordinates();
        EXPECT_TRUE(coord_sys.hasPolarizationCoordinate());

        if (coord_sys.hasPolarizationCoordinate()) {
            auto stokes_coord = coord_sys.stokesCoordinate();
            auto stokes_types = stokes_coord.stokes();
            EXPECT_EQ(stokes_types.size(), 1);
            for (auto stokes_type : stokes_types) {
                EXPECT_EQ(stokes_type, expected_stokes_type);
            }
        }
    }

    static CARTA::SetSpectralRequirements_SpectralConfig CursorSpectralConfig(const std::string& coordinate = "z") {
        CARTA::SetSpectralRequirements_SpectralConfig spectral_config;
        spectral_config.set_coordinate(coordinate);
        spectral_config.add_stats_types(CARTA::StatsType::Sum);
        return spectral_config;
    }

    static CARTA::SetSpectralRequirements_SpectralConfig RegionSpectralConfig(const std::string& coordinate = "z") {
        CARTA::SetSpectralRequirements_SpectralConfig spectral_config;
        spectral_config.set_coordinate(coordinate);
        spectral_config.add_stats_types(CARTA::StatsType::Mean);
        return spectral_config;
    }

    static void TestTotalPolarizedIntensity(const std::shared_ptr<casacore::ImageInterface<float>>& image) {
        // Calculate polarized intensity
        carta::PolarizationCalculator polarization_calculator(image);
        auto resulting_image = polarization_calculator.ComputeTotalPolarizedIntensity();
        CheckPolarizationType(resulting_image, casacore::Stokes::StokesTypes::Ptotal);

        // Get spectral axis size
        casacore::CoordinateSystem coord_sys = image->coordinates();
        int spectral_axis = coord_sys.spectralAxisNumber();
        int spectral_axis_size = image->shape()[spectral_axis];

        // Verify each pixel value from calculation results
        int stokes_q(1);
        int stokes_u(2);
        int stokes_v(3);
        int stokes_ptotal(0);
        std::vector<float> data_q;
        std::vector<float> data_u;
        std::vector<float> data_v;
        std::vector<float> data_results;

        for (int channel = 0; channel < spectral_axis_size; ++channel) {
            GetImageData(image, AxisRange(channel), stokes_q, data_q);
            GetImageData(image, AxisRange(channel), stokes_u, data_u);
            GetImageData(image, AxisRange(channel), stokes_v, data_v);
            GetImageData(resulting_image, AxisRange(channel), stokes_ptotal, data_results);

            EXPECT_EQ(data_results.size(), data_q.size());
            EXPECT_EQ(data_results.size(), data_u.size());
            EXPECT_EQ(data_results.size(), data_v.size());

            for (int i = 0; i < data_results.size(); ++i) {
                if (!isnan(data_results[i])) {
                    auto polarized_intensity = sqrt(pow(data_q[i], 2) + pow(data_u[i], 2) + pow(data_v[i], 2));
                    EXPECT_FLOAT_EQ(data_results[i], polarized_intensity);
                }
            }
        }
    }

    static void TestTotalFractionalPolarizedIntensity(const std::shared_ptr<casacore::ImageInterface<float>>& image) {
        // Calculate polarized intensity
        carta::PolarizationCalculator polarization_calculator(image);
        auto resulting_image = polarization_calculator.ComputeTotalFractionalPolarizedIntensity();
        CheckPolarizationType(resulting_image, casacore::Stokes::StokesTypes::PFtotal);

        // Get spectral axis size
        casacore::CoordinateSystem coord_sys = image->coordinates();
        int spectral_axis = coord_sys.spectralAxisNumber();
        int spectral_axis_size = image->shape()[spectral_axis];

        // Verify each pixel value from calculation results
        int stokes_i(0);
        int stokes_q(1);
        int stokes_u(2);
        int stokes_v(3);
        int stokes_pftotal(0);
        std::vector<float> data_i;
        std::vector<float> data_q;
        std::vector<float> data_u;
        std::vector<float> data_v;
        std::vector<float> data_results;

        for (int channel = 0; channel < spectral_axis_size; ++channel) {
            GetImageData(image, AxisRange(channel), stokes_i, data_i);
            GetImageData(image, AxisRange(channel), stokes_q, data_q);
            GetImageData(image, AxisRange(channel), stokes_u, data_u);
            GetImageData(image, AxisRange(channel), stokes_v, data_v);
            GetImageData(resulting_image, AxisRange(channel), stokes_pftotal, data_results);

            EXPECT_EQ(data_results.size(), data_i.size());
            EXPECT_EQ(data_results.size(), data_q.size());
            EXPECT_EQ(data_results.size(), data_u.size());
            EXPECT_EQ(data_results.size(), data_v.size());

            for (int i = 0; i < data_results.size(); ++i) {
                if (!isnan(data_results[i])) {
                    auto polarized_intensity = sqrt(pow(data_q[i], 2) + pow(data_u[i], 2) + pow(data_v[i], 2)) / data_i[i];
                    EXPECT_FLOAT_EQ(data_results[i], polarized_intensity);
                }
            }
        }
    }

    static void TestPolarizedIntensity(const std::shared_ptr<casacore::ImageInterface<float>>& image) {
        // Calculate polarized intensity
        carta::PolarizationCalculator polarization_calculator(image);
        auto resulting_image = polarization_calculator.ComputePolarizedIntensity();
        CheckPolarizationType(resulting_image, casacore::Stokes::StokesTypes::Plinear);

        // Get spectral axis size
        casacore::CoordinateSystem coord_sys = image->coordinates();
        int spectral_axis = coord_sys.spectralAxisNumber();
        int spectral_axis_size = image->shape()[spectral_axis];

        casacore::CoordinateSystem resulting_coord_sys = resulting_image->coordinates();
        EXPECT_EQ(coord_sys.nPixelAxes(), resulting_coord_sys.nPixelAxes());

        // Verify each pixel value from calculation results
        int stokes_q(1);
        int stokes_u(2);
        int stokes_pi(0);
        std::vector<float> data_q;
        std::vector<float> data_u;
        std::vector<float> data_results;

        for (int channel = 0; channel < spectral_axis_size; ++channel) {
            GetImageData(image, AxisRange(channel), stokes_q, data_q);
            GetImageData(image, AxisRange(channel), stokes_u, data_u);
            GetImageData(resulting_image, AxisRange(channel), stokes_pi, data_results);

            EXPECT_EQ(data_results.size(), data_q.size());
            EXPECT_EQ(data_results.size(), data_u.size());

            for (int i = 0; i < data_results.size(); ++i) {
                if (!isnan(data_results[i])) {
                    auto polarized_intensity = sqrt(pow(data_q[i], 2) + pow(data_u[i], 2));
                    EXPECT_FLOAT_EQ(data_results[i], polarized_intensity);
                }
            }
        }
    }

    static void TestPolarizedIntensityPerChannel(const std::shared_ptr<casacore::ImageInterface<float>>& image) {
        // Get spectral axis size
        casacore::CoordinateSystem coord_sys = image->coordinates();
        int spectral_axis = coord_sys.spectralAxisNumber();
        int spectral_axis_size = image->shape()[spectral_axis];

        // Verify each pixel value from calculation results
        int stokes_q(1);
        int stokes_u(2);
        int current_stokes(0);
        int current_channel(0);
        std::vector<float> data_q;
        std::vector<float> data_u;
        std::vector<float> data_results;
        int max_channel = (spectral_axis_size > MAX_CHANNEL) ? MAX_CHANNEL : spectral_axis_size;

        for (int channel = 0; channel < max_channel; ++channel) {
            // Calculate polarized intensity
            carta::PolarizationCalculator polarization_calculator(image, AxisRange(channel));
            auto resulting_image = polarization_calculator.ComputePolarizedIntensity();

            casacore::CoordinateSystem resulting_coord_sys = resulting_image->coordinates();
            EXPECT_EQ(coord_sys.nPixelAxes(), resulting_coord_sys.nPixelAxes());

            CheckPolarizationType(resulting_image, casacore::Stokes::StokesTypes::Plinear);

            GetImageData(image, AxisRange(channel), stokes_q, data_q);
            GetImageData(image, AxisRange(channel), stokes_u, data_u);
            GetImageData(resulting_image, AxisRange(current_channel), current_stokes, data_results);

            EXPECT_EQ(data_results.size(), data_q.size());
            EXPECT_EQ(data_results.size(), data_u.size());

            for (int i = 0; i < data_results.size(); ++i) {
                if (!isnan(data_results[i])) {
                    auto polarized_intensity = sqrt(pow(data_q[i], 2) + pow(data_u[i], 2));
                    EXPECT_FLOAT_EQ(data_results[i], polarized_intensity);
                }
            }
        }
    }

    static void TestPolarizedIntensityPerChunk(const std::shared_ptr<casacore::ImageInterface<float>>& image) {
        // Get spectral axis size
        casacore::CoordinateSystem coord_sys = image->coordinates();
        int spectral_axis = coord_sys.spectralAxisNumber();
        int spectral_axis_size = image->shape()[spectral_axis];

        int start_channel = spectral_axis_size / 2;
        int end_channel = spectral_axis_size - 1;

        // Calculate polarized intensity
        carta::PolarizationCalculator polarization_calculator(image, AxisRange(start_channel, end_channel));
        auto resulting_image = polarization_calculator.ComputePolarizedIntensity();
        CheckPolarizationType(resulting_image, casacore::Stokes::StokesTypes::Plinear);

        // Verify each pixel value from calculation results
        int stokes_q(1);
        int stokes_u(2);
        int stokes_pi(0);
        std::vector<float> data_q;
        std::vector<float> data_u;
        std::vector<float> data_results;

        // Check per channel data
        for (int channel = start_channel; channel <= end_channel; ++channel) {
            GetImageData(image, AxisRange(channel), stokes_q, data_q);
            GetImageData(image, AxisRange(channel), stokes_u, data_u);
            GetImageData(resulting_image, AxisRange(channel - start_channel), stokes_pi, data_results);

            casacore::CoordinateSystem resulting_coord_sys = resulting_image->coordinates();
            EXPECT_EQ(coord_sys.nPixelAxes(), resulting_coord_sys.nPixelAxes());

            EXPECT_EQ(data_results.size(), data_q.size());
            EXPECT_EQ(data_results.size(), data_u.size());

            for (int i = 0; i < data_results.size(); ++i) {
                if (!isnan(data_results[i])) {
                    auto polarized_intensity = sqrt(pow(data_q[i], 2) + pow(data_u[i], 2));
                    EXPECT_FLOAT_EQ(data_results[i], polarized_intensity);
                }
            }
        }

        // Check chunk data
        std::vector<float> chunk_data_results;
        GetImageData(resulting_image, AxisRange(), stokes_pi, chunk_data_results);

        for (int chunk_data_index = 0, channel = start_channel; channel <= end_channel; ++channel) {
            GetImageData(image, AxisRange(channel), stokes_q, data_q);
            GetImageData(image, AxisRange(channel), stokes_u, data_u);

            EXPECT_EQ(data_q.size(), data_u.size());

            for (int i = 0; i < data_q.size(); ++i) {
                if (!isnan(data_q[i]) && !isnan(data_u[i])) {
                    auto polarized_intensity = sqrt(pow(data_q[i], 2) + pow(data_u[i], 2));
                    EXPECT_FLOAT_EQ(chunk_data_results[chunk_data_index], polarized_intensity);
                }
                ++chunk_data_index;
            }
        }
    }

    static void TestFractionalPolarizedIntensity(const std::shared_ptr<casacore::ImageInterface<float>>& image) {
        // Calculate polarized intensity
        carta::PolarizationCalculator polarization_calculator(image);
        auto resulting_image = polarization_calculator.ComputeFractionalPolarizedIntensity();
        CheckPolarizationType(resulting_image, casacore::Stokes::StokesTypes::PFlinear);

        // Get spectral axis size
        casacore::CoordinateSystem coord_sys = image->coordinates();
        int spectral_axis = coord_sys.spectralAxisNumber();
        int spectral_axis_size = image->shape()[spectral_axis];

        // Verify each pixel value from calculation results
        int stokes_i(0);
        int stokes_q(1);
        int stokes_u(2);
        int stokes_fpi(0);
        std::vector<float> data_i;
        std::vector<float> data_q;
        std::vector<float> data_u;
        std::vector<float> data_results;

        for (int channel = 0; channel < spectral_axis_size; ++channel) {
            GetImageData(image, AxisRange(channel), stokes_i, data_i);
            GetImageData(image, AxisRange(channel), stokes_q, data_q);
            GetImageData(image, AxisRange(channel), stokes_u, data_u);
            GetImageData(resulting_image, AxisRange(channel), stokes_fpi, data_results);

            EXPECT_EQ(data_results.size(), data_i.size());
            EXPECT_EQ(data_results.size(), data_q.size());
            EXPECT_EQ(data_results.size(), data_u.size());

            for (int i = 0; i < data_results.size(); ++i) {
                if (!isnan(data_results[i])) {
                    auto polarized_intensity = sqrt(pow(data_q[i], 2) + pow(data_u[i], 2)) / data_i[i];
                    EXPECT_FLOAT_EQ(data_results[i], polarized_intensity);
                }
            }
        }
    }

    static void TestFractionalPolarizedIntensityPerChannel(const std::shared_ptr<casacore::ImageInterface<float>>& image) {
        // Get spectral axis size
        casacore::CoordinateSystem coord_sys = image->coordinates();
        int spectral_axis = coord_sys.spectralAxisNumber();
        int spectral_axis_size = image->shape()[spectral_axis];

        // Verify each pixel value from calculation results
        int stokes_i(0);
        int stokes_q(1);
        int stokes_u(2);
        int current_stokes(0);
        int current_channel(0);
        std::vector<float> data_i;
        std::vector<float> data_q;
        std::vector<float> data_u;
        std::vector<float> data_results;
        int max_channel = (spectral_axis_size > MAX_CHANNEL) ? MAX_CHANNEL : spectral_axis_size;

        for (int channel = 0; channel < max_channel; ++channel) {
            // Calculate polarized intensity
            carta::PolarizationCalculator polarization_calculator(image, AxisRange(channel));
            auto resulting_image = polarization_calculator.ComputeFractionalPolarizedIntensity();
            CheckPolarizationType(resulting_image, casacore::Stokes::StokesTypes::PFlinear);

            GetImageData(image, AxisRange(channel), stokes_i, data_i);
            GetImageData(image, AxisRange(channel), stokes_q, data_q);
            GetImageData(image, AxisRange(channel), stokes_u, data_u);
            GetImageData(resulting_image, AxisRange(current_channel), current_stokes, data_results);

            EXPECT_EQ(data_results.size(), data_i.size());
            EXPECT_EQ(data_results.size(), data_q.size());
            EXPECT_EQ(data_results.size(), data_u.size());

            for (int i = 0; i < data_results.size(); ++i) {
                if (!isnan(data_results[i])) {
                    auto polarized_intensity = sqrt(pow(data_q[i], 2) + pow(data_u[i], 2)) / data_i[i];
                    EXPECT_FLOAT_EQ(data_results[i], polarized_intensity);
                }
            }
        }
    }

    static void TestPolarizedAngle(const std::shared_ptr<casacore::ImageInterface<float>>& image, bool radiant) {
        carta::PolarizationCalculator polarization_calculator(image);
        // Calculate polarized angle
        auto resulting_image = polarization_calculator.ComputePolarizedAngle(radiant);
        CheckPolarizationType(resulting_image, casacore::Stokes::StokesTypes::Pangle);

        // Get spectral axis size
        casacore::CoordinateSystem coord_sys = image->coordinates();
        int spectral_axis = coord_sys.spectralAxisNumber();
        int spectral_axis_size = image->shape()[spectral_axis];

        // Verify each pixel value from calculation results
        int stokes_q(1);
        int stokes_u(2);
        int stokes_pa(0);
        std::vector<float> data_q;
        std::vector<float> data_u;
        std::vector<float> data_results;

        for (int channel = 0; channel < spectral_axis_size; ++channel) {
            GetImageData(image, AxisRange(channel), stokes_q, data_q);
            GetImageData(image, AxisRange(channel), stokes_u, data_u);
            GetImageData(resulting_image, AxisRange(channel), stokes_pa, data_results);

            EXPECT_EQ(data_results.size(), data_q.size());
            EXPECT_EQ(data_results.size(), data_u.size());

            for (int i = 0; i < data_results.size(); ++i) {
                if (!isnan(data_results[i])) {
                    auto polarized_angle = atan2(data_u[i], data_q[i]) / 2;
                    if (!radiant) {
                        polarized_angle = polarized_angle * 180 / C::pi; // as degree value
                    }
                    EXPECT_FLOAT_EQ(data_results[i], polarized_angle);
                }
            }
        }
    }

    static void TestPolarizedAnglePerChannel(const std::shared_ptr<casacore::ImageInterface<float>>& image, bool radiant) {
        // Get spectral axis size
        casacore::CoordinateSystem coord_sys = image->coordinates();
        int spectral_axis = coord_sys.spectralAxisNumber();
        int spectral_axis_size = image->shape()[spectral_axis];

        // Verify each pixel value from calculation results
        int stokes_q(1);
        int stokes_u(2);
        int current_stokes(0);
        int current_channel(0);
        std::vector<float> data_q;
        std::vector<float> data_u;
        std::vector<float> data_results;
        int max_channel = (spectral_axis_size > MAX_CHANNEL) ? MAX_CHANNEL : spectral_axis_size;

        for (int channel = 0; channel < max_channel; ++channel) {
            // Calculate polarized angle
            carta::PolarizationCalculator polarization_calculator(image, AxisRange(channel));
            auto resulting_image = polarization_calculator.ComputePolarizedAngle(radiant);
            CheckPolarizationType(resulting_image, casacore::Stokes::StokesTypes::Pangle);

            GetImageData(image, AxisRange(channel), stokes_q, data_q);
            GetImageData(image, AxisRange(channel), stokes_u, data_u);
            GetImageData(resulting_image, AxisRange(current_channel), current_stokes, data_results);

            EXPECT_EQ(data_results.size(), data_q.size());
            EXPECT_EQ(data_results.size(), data_u.size());

            for (int i = 0; i < data_results.size(); ++i) {
                if (!isnan(data_results[i])) {
                    auto polarized_angle = atan2(data_u[i], data_q[i]) / 2;
                    if (!radiant) {
                        polarized_angle = polarized_angle * 180 / C::pi; // as degree value
                    }
                    EXPECT_FLOAT_EQ(data_results[i], polarized_angle);
                }
            }
        }
    }

    static void TestPerformances(const std::shared_ptr<casacore::ImageInterface<float>>& image) {
        // Get spectral axis size
        casacore::CoordinateSystem coord_sys = image->coordinates();
        int spectral_axis = coord_sys.spectralAxisNumber();
        int spectral_axis_size = image->shape()[spectral_axis];
        int max_channel = (spectral_axis_size > MAX_CHANNEL) ? MAX_CHANNEL : spectral_axis_size;

        // Calculate polarized intensity per cube
        auto t_start_per_cube = std::chrono::high_resolution_clock::now();

        carta::PolarizationCalculator polarization_calculator(image, AxisRange(0, max_channel - 1));
        auto image_pi_per_cube = polarization_calculator.ComputePolarizedIntensity();
        auto image_fpi_per_cube = polarization_calculator.ComputeFractionalPolarizedIntensity();
        auto image_pa_per_cube = polarization_calculator.ComputePolarizedAngle();

        auto t_end_per_cube = std::chrono::high_resolution_clock::now();
        auto dt_per_cube = std::chrono::duration_cast<std::chrono::microseconds>(t_end_per_cube - t_start_per_cube).count();

        // Calculate polarized intensity per channel
        auto t_start_per_channel = std::chrono::high_resolution_clock::now();

        for (int channel = 0; channel < max_channel; ++channel) {
            carta::PolarizationCalculator polarization_calculator_per_channel(image, AxisRange(channel));
            auto image_pi_per_channel = polarization_calculator_per_channel.ComputePolarizedIntensity();
            auto image_fpi_per_channel = polarization_calculator_per_channel.ComputeFractionalPolarizedIntensity();
            auto image_pa_per_channel = polarization_calculator_per_channel.ComputePolarizedAngle();
        }

        auto t_end_per_channel = std::chrono::high_resolution_clock::now();
        auto dt_per_channel = std::chrono::duration_cast<std::chrono::microseconds>(t_end_per_channel - t_start_per_channel).count();

        EXPECT_LT(dt_per_cube, dt_per_channel);

        spdlog::info("Calculate polarized intensity per cube/channel spends: {:.3f}/{:.3f} ms", dt_per_cube * 1e-3, dt_per_channel * 1e-3);
    }

    static void TestConsistency(const std::shared_ptr<casacore::ImageInterface<float>>& image) {
        // Get spectral axis size
        casacore::CoordinateSystem coord_sys = image->coordinates();
        int spectral_axis = coord_sys.spectralAxisNumber();
        int spectral_axis_size = image->shape()[spectral_axis];
        int max_channel = (spectral_axis_size > MAX_CHANNEL) ? MAX_CHANNEL : spectral_axis_size;

        // Calculate polarized intensity per cube
        carta::PolarizationCalculator polarization_calculator(image, AxisRange(0, max_channel - 1));
        auto image_pi_per_cube = polarization_calculator.ComputePolarizedIntensity();
        auto image_fpi_per_cube = polarization_calculator.ComputeFractionalPolarizedIntensity();
        auto image_pa_per_cube = polarization_calculator.ComputePolarizedAngle();

        int current_stokes(0);
        int current_channel(0);
        std::vector<float> data_pi_per_cube;
        std::vector<float> data_fpi_per_cube;
        std::vector<float> data_pa_per_cube;
        std::vector<float> data_pi_per_channel;
        std::vector<float> data_fpi_per_channel;
        std::vector<float> data_pa_per_channel;

        // Calculate polarized intensity per channel
        for (int channel = 0; channel < max_channel; ++channel) {
            carta::PolarizationCalculator polarization_calculator_per_channel(image, AxisRange(channel));
            auto image_pi_per_channel = polarization_calculator_per_channel.ComputePolarizedIntensity();
            auto image_fpi_per_channel = polarization_calculator_per_channel.ComputeFractionalPolarizedIntensity();
            auto image_pa_per_channel = polarization_calculator_per_channel.ComputePolarizedAngle();

            GetImageData(image_pi_per_cube, AxisRange(channel), current_stokes, data_pi_per_cube);
            GetImageData(image_pi_per_channel, AxisRange(current_channel), current_stokes, data_pi_per_channel);

            EXPECT_EQ(data_pi_per_cube.size(), data_pi_per_channel.size());
            if (data_pi_per_cube.size() == data_pi_per_channel.size()) {
                for (int i = 0; i < data_pi_per_cube.size(); ++i) {
                    if (!isnan(data_pi_per_cube[i]) && !isnan(data_pi_per_channel[i])) {
                        EXPECT_FLOAT_EQ(data_pi_per_cube[i], data_pi_per_channel[i]);
                    }
                }
            }

            GetImageData(image_fpi_per_cube, AxisRange(channel), current_stokes, data_fpi_per_cube);
            GetImageData(image_fpi_per_channel, AxisRange(current_channel), current_stokes, data_fpi_per_channel);

            EXPECT_EQ(data_fpi_per_cube.size(), data_fpi_per_channel.size());
            if (data_fpi_per_cube.size() == data_fpi_per_channel.size()) {
                for (int i = 0; i < data_fpi_per_cube.size(); ++i) {
                    if (!isnan(data_fpi_per_cube[i]) && !isnan(data_fpi_per_channel[i])) {
                        EXPECT_FLOAT_EQ(data_fpi_per_cube[i], data_fpi_per_channel[i]);
                    }
                }
            }

            GetImageData(image_pa_per_cube, AxisRange(channel), current_stokes, data_pa_per_cube);
            GetImageData(image_pa_per_channel, AxisRange(current_channel), current_stokes, data_pa_per_channel);

            EXPECT_EQ(data_pa_per_cube.size(), data_pa_per_channel.size());
            if (data_pa_per_cube.size() == data_pa_per_channel.size()) {
                for (int i = 0; i < data_pa_per_cube.size(); ++i) {
                    if (!isnan(data_pa_per_cube[i]) && !isnan(data_pa_per_channel[i])) {
                        EXPECT_FLOAT_EQ(data_pa_per_cube[i], data_pa_per_channel[i]);
                    }
                }
            }
        }
    }

    static void VerifyFrameImageCache(
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
                EXPECT_FLOAT_EQ(data1[i], data2[i]);
            }
        }
    }
};

class TestFrame : public Frame {
public:
    TestFrame(uint32_t session_id, carta::FileLoader* loader, const std::string& hdu, int default_z = DEFAULT_Z)
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
        std::unique_ptr<TestFrame> frame(new TestFrame(0, carta::FileLoader::GetLoader(sample_file_path), "0"));
        EXPECT_TRUE(frame->IsValid());
        EXPECT_TRUE(frame->_open_image_error.empty());

        // Set stokes to be calculated
        std::string message;
        int max_channel = (spectral_axis_size > MAX_CHANNEL) ? MAX_CHANNEL : spectral_axis_size;

        for (int channel = 0; channel < max_channel; ++channel) {
            frame->SetImageChannels(channel, COMPUTE_STOKES_PTOTAL, message);
            PolarizationCalculatorTest::VerifyFrameImageCache(image, channel, COMPUTE_STOKES_PTOTAL, frame->_image_cache);

            frame->SetImageChannels(channel, COMPUTE_STOKES_PFTOTAL, message);
            PolarizationCalculatorTest::VerifyFrameImageCache(image, channel, COMPUTE_STOKES_PFTOTAL, frame->_image_cache);

            frame->SetImageChannels(channel, COMPUTE_STOKES_PLINEAR, message);
            PolarizationCalculatorTest::VerifyFrameImageCache(image, channel, COMPUTE_STOKES_PLINEAR, frame->_image_cache);

            frame->SetImageChannels(channel, COMPUTE_STOKES_PFLINEAR, message);
            PolarizationCalculatorTest::VerifyFrameImageCache(image, channel, COMPUTE_STOKES_PFLINEAR, frame->_image_cache);

            frame->SetImageChannels(channel, COMPUTE_STOKES_PANGLE, message);
            PolarizationCalculatorTest::VerifyFrameImageCache(image, channel, COMPUTE_STOKES_PANGLE, frame->_image_cache);
        }
    }
};

static void TestCursorProfiles(std::string sample_file_path, int current_channel, int current_stokes, int config_stokes,
    std::string stokes_config_x, std::string stokes_config_y, std::string stokes_config_z) {
    std::string reference_file_path = FileFinder::CasaImagePath(SAMPLE_IMAGE_CASA);
    std::shared_ptr<casacore::ImageInterface<float>> image;
    if (!OpenImage(image, reference_file_path)) {
        return;
    }

    // Get directional axis size
    int x_size = image->shape()[0];
    int y_size = image->shape()[1];

    // Open the file through the Frame
    std::unique_ptr<TestFrame> frame(new TestFrame(0, carta::FileLoader::GetLoader(sample_file_path), "0"));
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
    auto data_profiles = PolarizationCalculatorTest::GetSpatialProfiles(image, current_channel, config_stokes, cursor_x, cursor_y);

    // Check the consistency of two ways
    PolarizationCalculatorTest::CompareData(data_vec, data_profiles);

    // Set spectral configs for the cursor
    std::vector<CARTA::SetSpectralRequirements_SpectralConfig> spectral_configs{
        PolarizationCalculatorTest::CursorSpectralConfig(stokes_config_z)};
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

    std::vector<float> spectral_profile_data_1 = PolarizationCalculatorTest::SpectralProfileValues(spectral_profile);

    // Get spatial profiles by another way
    int stokes = stokes_config_z == "z" ? current_stokes : config_stokes;
    std::vector<float> spectral_profile_data_2 =
        PolarizationCalculatorTest::GetCursorSpectralProfiles(image, AxisRange(ALL_Z), stokes, cursor_x, cursor_y);

    // Check the consistency of two ways
    PolarizationCalculatorTest::CompareData(spectral_profile_data_1, spectral_profile_data_2);
}

static void TestPointRegionProfiles(std::string sample_file_path, int current_channel, int current_stokes, int config_stokes,
    std::string stokes_config_x, std::string stokes_config_y, std::string stokes_config_z) {
    std::string reference_file_path = FileFinder::CasaImagePath(SAMPLE_IMAGE_CASA);
    std::shared_ptr<casacore::ImageInterface<float>> image;
    if (!OpenImage(image, reference_file_path)) {
        return;
    }

    // Open a sample image through the Frame
    if (!fs::exists(sample_file_path)) {
        return;
    }

    int file_id(0);
    auto frame = std::make_shared<TestFrame>(file_id, carta::FileLoader::GetLoader(sample_file_path), "0");
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
    auto data_profiles = PolarizationCalculatorTest::GetSpatialProfiles(image, current_channel, config_stokes, cursor_x, cursor_y);

    // Compare data
    PolarizationCalculatorTest::CompareData(spatial_profile_data_vec, data_profiles);

    // Set spectral configs for a point region
    std::vector<CARTA::SetSpectralRequirements_SpectralConfig> spectral_configs{
        PolarizationCalculatorTest::CursorSpectralConfig(stokes_config_z)};
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

    std::vector<double> tmp = PolarizationCalculatorTest::SpectralProfileDoubleValues(spectral_profile);
    // convert the double type vector to the float type vector
    std::vector<float> spectral_profile_data_1(tmp.begin(), tmp.end());

    // Get spectral profiles by another way
    int stokes = stokes_config_z == "z" ? current_stokes : config_stokes;
    std::vector<float> spectral_profile_data_2 =
        PolarizationCalculatorTest::GetCursorSpectralProfiles(image, AxisRange(ALL_Z), stokes, cursor_x, cursor_y);

    // Check the consistency of two ways
    PolarizationCalculatorTest::CompareData(spectral_profile_data_1, spectral_profile_data_2);
}

static void TestRectangleRegionProfiles(
    std::string sample_file_path, int current_channel, int current_stokes, const std::string& stokes_config_z) {
    // Open a reference image
    std::string reference_file_path = FileFinder::CasaImagePath(SAMPLE_IMAGE_CASA);
    std::shared_ptr<casacore::ImageInterface<float>> image;
    if (!OpenImage(image, reference_file_path)) {
        return;
    }

    // Open a sample image through the Frame
    if (!fs::exists(sample_file_path)) {
        return;
    }

    int file_id(0);
    auto frame = std::make_shared<TestFrame>(file_id, carta::FileLoader::GetLoader(sample_file_path), "0");
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
    std::vector<CARTA::SetSpectralRequirements_SpectralConfig> spectral_configs{
        PolarizationCalculatorTest::RegionSpectralConfig(stokes_config_z)};
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

    std::vector<double> spectral_profile_data_double = PolarizationCalculatorTest::SpectralProfileDoubleValues(spectral_profile);
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
        PolarizationCalculatorTest::GetImageData(resulting_image, AxisRange(channel), stokes, tmp_data);
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
    PolarizationCalculatorTest::CompareData(spectral_profile_data_float, spectral_profile_data_float_2);
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
    const std::shared_ptr<TestFrame>& frame, int current_channel, int current_stokes, carta::Histogram& cube_histogram) {
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
    std::string reference_file_path = FileFinder::CasaImagePath(SAMPLE_IMAGE_CASA);
    std::shared_ptr<casacore::ImageInterface<float>> image;
    if (!OpenImage(image, reference_file_path)) {
        return;
    }

    // Open a sample image through the Frame
    if (!fs::exists(sample_file_path)) {
        return;
    }

    // Calculate the cube histogram
    auto frame = std::make_shared<TestFrame>(0, carta::FileLoader::GetLoader(sample_file_path), "0");
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

    std::unique_ptr<carta::FileLoader> loader;
    loader.reset(carta::FileLoader::GetLoader(resulting_image));

    auto frame2 = std::make_shared<TestFrame>(1, loader.get(), "");
    loader.release();
    int fiddled_stokes(0);

    carta::Histogram cube_histogram2;
    CalculateCubeHistogram(frame2, current_channel, fiddled_stokes, cube_histogram2);

    Compare(cube_histogram, cube_histogram2);
}

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
}

TEST_F(PolarizationCalculatorTest, TestTotalPolarizedIntensity) {
    std::string file_path = FitsImagePath(SAMPLE_IMAGE_FITS);
    std::shared_ptr<casacore::ImageInterface<float>> image;
    if (OpenImage(image, file_path)) {
        TestTotalPolarizedIntensity(image);
    }
}

TEST_F(PolarizationCalculatorTest, TestTotalFractionalPolarizedIntensity) {
    std::string file_path = FitsImagePath(SAMPLE_IMAGE_FITS);
    std::shared_ptr<casacore::ImageInterface<float>> image;
    if (OpenImage(image, file_path)) {
        TestTotalFractionalPolarizedIntensity(image);
    }
}

TEST_F(PolarizationCalculatorTest, TestPolarizedIntensity) {
    std::string file_path = FitsImagePath(SAMPLE_IMAGE_FITS);
    std::shared_ptr<casacore::ImageInterface<float>> image;
    if (OpenImage(image, file_path)) {
        TestPolarizedIntensity(image);
    }
}

TEST_F(PolarizationCalculatorTest, TestPolarizedIntensityPerChannel) {
    std::string file_path = FitsImagePath(SAMPLE_IMAGE_FITS);
    std::shared_ptr<casacore::ImageInterface<float>> image;
    if (OpenImage(image, file_path)) {
        TestPolarizedIntensityPerChannel(image);
    }
}

TEST_F(PolarizationCalculatorTest, TestPolarizedIntensityPerChunk) {
    std::string file_path = FitsImagePath(SAMPLE_IMAGE_FITS);
    std::shared_ptr<casacore::ImageInterface<float>> image;
    if (OpenImage(image, file_path)) {
        TestPolarizedIntensityPerChunk(image);
    }
}

TEST_F(PolarizationCalculatorTest, TestFractionalPolarizedIntensity) {
    std::string file_path = FitsImagePath(SAMPLE_IMAGE_FITS);
    std::shared_ptr<casacore::ImageInterface<float>> image;
    if (OpenImage(image, file_path)) {
        TestFractionalPolarizedIntensity(image);
    }
}

TEST_F(PolarizationCalculatorTest, TestFractionalPolarizedIntensityPerChannel) {
    std::string file_path = FitsImagePath(SAMPLE_IMAGE_FITS);
    std::shared_ptr<casacore::ImageInterface<float>> image;
    if (OpenImage(image, file_path)) {
        TestFractionalPolarizedIntensityPerChannel(image);
    }
}

TEST_F(PolarizationCalculatorTest, TestPolarizedAngle) {
    std::string file_path = FitsImagePath(SAMPLE_IMAGE_FITS);
    std::shared_ptr<casacore::ImageInterface<float>> image;
    if (OpenImage(image, file_path)) {
        TestPolarizedAngle(image, true);
        TestPolarizedAngle(image, false);
    }
}

TEST_F(PolarizationCalculatorTest, TestPolarizedAnglePerChannel) {
    std::string file_path = FitsImagePath(SAMPLE_IMAGE_FITS);
    std::shared_ptr<casacore::ImageInterface<float>> image;
    if (OpenImage(image, file_path)) {
        TestPolarizedAnglePerChannel(image, true);
        TestPolarizedAnglePerChannel(image, false);
    }
}

TEST_F(PolarizationCalculatorTest, TestPerformances) {
    std::string file_path = FitsImagePath(SAMPLE_IMAGE_FITS);
    std::shared_ptr<casacore::ImageInterface<float>> image;
    if (OpenImage(image, file_path)) {
        TestPerformances(image);
    }
}

TEST_F(PolarizationCalculatorTest, TestConsistency) {
    std::string file_path = FitsImagePath(SAMPLE_IMAGE_FITS);
    std::shared_ptr<casacore::ImageInterface<float>> image;
    if (OpenImage(image, file_path)) {
        TestConsistency(image);
    }
}

TEST_F(PolarizationCalculatorTest, TestFrameImageCache) {
    TestFrame::TestFrameImageCache(FileFinder::CasaImagePath(SAMPLE_IMAGE_CASA));
    TestFrame::TestFrameImageCache(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS));
    TestFrame::TestFrameImageCache(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5));
}

TEST_F(PolarizationCalculatorTest, TestCursorProfiles) {
    TestCursorProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 1, 0, COMPUTE_STOKES_PTOTAL, "Ptotalx", "Ptotaly", "Ptotalz");
    TestCursorProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 1, 0, COMPUTE_STOKES_PFTOTAL, "PFtotalx", "PFtotaly", "PFtotalz");
    TestCursorProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 1, 0, COMPUTE_STOKES_PLINEAR, "Plinearx", "Plineary", "Plinearz");
    TestCursorProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 1, 0, COMPUTE_STOKES_PFLINEAR, "PFlinearx", "PFlineary", "PFlinearz");
    TestCursorProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 1, 0, COMPUTE_STOKES_PANGLE, "Panglex", "Pangley", "Panglez");

    TestCursorProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 1, 0, 0, "Ix", "Iy", "Iz");
    TestCursorProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 1, 0, 1, "Qx", "Qy", "Qz");
    TestCursorProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 1, 0, 2, "Ux", "Uy", "Uz");
    TestCursorProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 1, 0, 3, "Vx", "Vy", "Vz");

    TestCursorProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 1, 0, COMPUTE_STOKES_PTOTAL, "Ptotalx", "Ptotaly", "z");
    TestCursorProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 1, 0, COMPUTE_STOKES_PFTOTAL, "PFtotalx", "PFtotaly", "z");
    TestCursorProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 1, 0, COMPUTE_STOKES_PLINEAR, "Plinearx", "Plineary", "z");
    TestCursorProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 1, 0, COMPUTE_STOKES_PFLINEAR, "PFlinearx", "PFlineary", "z");
    TestCursorProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 1, 0, COMPUTE_STOKES_PANGLE, "Panglex", "Pangley", "z");

    TestCursorProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 1, 0, 0, "Ix", "Iy", "z");
    TestCursorProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 1, 0, 1, "Qx", "Qy", "z");
    TestCursorProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 1, 0, 2, "Ux", "Uy", "z");
    TestCursorProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 1, 0, 3, "Vx", "Vy", "z");

    TestCursorProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 1, 0, COMPUTE_STOKES_PTOTAL, "Ptotalx", "Ptotaly", "Ptotalz");
    TestCursorProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 1, 0, COMPUTE_STOKES_PFTOTAL, "PFtotalx", "PFtotaly", "PFtotalz");
    TestCursorProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 1, 0, COMPUTE_STOKES_PLINEAR, "Plinearx", "Plineary", "Plinearz");
    TestCursorProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 1, 0, COMPUTE_STOKES_PFLINEAR, "PFlinearx", "PFlineary", "PFlinearz");
    TestCursorProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 1, 0, COMPUTE_STOKES_PANGLE, "Panglex", "Pangley", "Panglez");

    TestCursorProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 1, 0, 0, "Ix", "Iy", "Iz");
    TestCursorProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 1, 0, 1, "Qx", "Qy", "Qz");
    TestCursorProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 1, 0, 2, "Ux", "Uy", "Uz");
    TestCursorProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 1, 0, 3, "Vx", "Vy", "Vz");

    TestCursorProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 1, 0, COMPUTE_STOKES_PTOTAL, "Ptotalx", "Ptotaly", "z");
    TestCursorProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 1, 0, COMPUTE_STOKES_PFTOTAL, "PFtotalx", "PFtotaly", "z");
    TestCursorProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 1, 0, COMPUTE_STOKES_PLINEAR, "Plinearx", "Plineary", "z");
    TestCursorProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 1, 0, COMPUTE_STOKES_PFLINEAR, "PFlinearx", "PFlineary", "z");
    TestCursorProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 1, 0, COMPUTE_STOKES_PANGLE, "Panglex", "Pangley", "z");

    TestCursorProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 1, 0, 0, "Ix", "Iy", "z");
    TestCursorProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 1, 0, 1, "Qx", "Qy", "z");
    TestCursorProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 1, 0, 2, "Ux", "Uy", "z");
    TestCursorProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 1, 0, 3, "Vx", "Vy", "z");
}

TEST_F(PolarizationCalculatorTest, TestPointRegionProfiles) {
    TestPointRegionProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, COMPUTE_STOKES_PTOTAL, "Ptotalx", "Ptotaly", "Ptotalz");
    TestPointRegionProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, COMPUTE_STOKES_PFTOTAL, "PFtotalx", "PFtotaly", "PFtotalz");
    TestPointRegionProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, COMPUTE_STOKES_PLINEAR, "Plinearx", "Plineary", "Plinearz");
    TestPointRegionProfiles(
        FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, COMPUTE_STOKES_PFLINEAR, "PFlinearx", "PFlineary", "PFlinearz");
    TestPointRegionProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, COMPUTE_STOKES_PANGLE, "Panglex", "Pangley", "Panglez");

    TestPointRegionProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, COMPUTE_STOKES_PTOTAL, "Ptotalx", "Ptotaly", "Ptotalz");
    TestPointRegionProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, COMPUTE_STOKES_PFTOTAL, "PFtotalx", "PFtotaly", "PFtotalz");
    TestPointRegionProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, COMPUTE_STOKES_PLINEAR, "Plinearx", "Plineary", "Plinearz");
    TestPointRegionProfiles(
        FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, COMPUTE_STOKES_PFLINEAR, "PFlinearx", "PFlineary", "PFlinearz");
    TestPointRegionProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, COMPUTE_STOKES_PANGLE, "Panglex", "Pangley", "Panglez");

    TestPointRegionProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, 0, "Ix", "Iy", "Iz");
    TestPointRegionProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, 1, "Qx", "Qy", "Qz");
    TestPointRegionProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, 2, "Ux", "Uy", "Uz");
    TestPointRegionProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, 3, "Vx", "Vy", "Vz");

    TestPointRegionProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, 0, "Ix", "Iy", "Iz");
    TestPointRegionProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, 1, "Qx", "Qy", "Qz");
    TestPointRegionProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, 2, "Ux", "Uy", "Uz");
    TestPointRegionProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, 3, "Vx", "Vy", "Vz");

    TestPointRegionProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, COMPUTE_STOKES_PTOTAL, "Ptotalx", "Ptotaly", "z");
    TestPointRegionProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, COMPUTE_STOKES_PFTOTAL, "PFtotalx", "PFtotaly", "z");
    TestPointRegionProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, COMPUTE_STOKES_PLINEAR, "Plinearx", "Plineary", "z");
    TestPointRegionProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, COMPUTE_STOKES_PFLINEAR, "PFlinearx", "PFlineary", "z");
    TestPointRegionProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, COMPUTE_STOKES_PANGLE, "Panglex", "Pangley", "z");

    TestPointRegionProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, COMPUTE_STOKES_PTOTAL, "Ptotalx", "Ptotaly", "z");
    TestPointRegionProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, COMPUTE_STOKES_PFTOTAL, "PFtotalx", "PFtotaly", "z");
    TestPointRegionProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, COMPUTE_STOKES_PLINEAR, "Plinearx", "Plineary", "z");
    TestPointRegionProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, COMPUTE_STOKES_PFLINEAR, "PFlinearx", "PFlineary", "z");
    TestPointRegionProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, COMPUTE_STOKES_PANGLE, "Panglex", "Pangley", "z");

    TestPointRegionProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, 0, "Ix", "Iy", "z");
    TestPointRegionProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, 1, "Qx", "Qy", "z");
    TestPointRegionProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, 2, "Ux", "Uy", "z");
    TestPointRegionProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, 3, "Vx", "Vy", "z");

    TestPointRegionProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, 0, "Ix", "Iy", "z");
    TestPointRegionProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, 1, "Qx", "Qy", "z");
    TestPointRegionProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, 2, "Ux", "Uy", "z");
    TestPointRegionProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, 3, "Vx", "Vy", "z");
}

TEST_F(PolarizationCalculatorTest, TestRectangleRegionProfiles) {
    TestRectangleRegionProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, "Ptotalz");
    TestRectangleRegionProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, "PFtotalz");
    TestRectangleRegionProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, "Plinearz");
    TestRectangleRegionProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, "PFlinearz");
    TestRectangleRegionProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, "Panglez");

    TestRectangleRegionProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, "Iz");
    TestRectangleRegionProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, "Qz");
    TestRectangleRegionProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, "Uz");
    TestRectangleRegionProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, "Vz");

    TestRectangleRegionProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 0, 0, "z");
    TestRectangleRegionProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 0, 1, "z");
    TestRectangleRegionProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 0, 2, "z");
    TestRectangleRegionProfiles(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 0, 3, "z");

    TestRectangleRegionProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, "Ptotalz");
    TestRectangleRegionProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, "PFtotalz");
    TestRectangleRegionProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, "Plinearz");
    TestRectangleRegionProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, "PFlinearz");
    TestRectangleRegionProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, "Panglez");

    TestRectangleRegionProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, "Iz");
    TestRectangleRegionProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, "Qz");
    TestRectangleRegionProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, "Uz");
    TestRectangleRegionProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, "Vz");

    TestRectangleRegionProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 0, "z");
    TestRectangleRegionProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 1, "z");
    TestRectangleRegionProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 2, "z");
    TestRectangleRegionProfiles(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, 3, "z");
}

TEST_F(PolarizationCalculatorTest, TestCubeHistogram) {
    TestCubeHistogram(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 0, COMPUTE_STOKES_PTOTAL);
    TestCubeHistogram(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 0, COMPUTE_STOKES_PFTOTAL);
    TestCubeHistogram(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 0, COMPUTE_STOKES_PLINEAR);
    TestCubeHistogram(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 0, COMPUTE_STOKES_PFLINEAR);
    TestCubeHistogram(FileFinder::FitsImagePath(SAMPLE_IMAGE_FITS), 0, COMPUTE_STOKES_PANGLE);

    TestCubeHistogram(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, COMPUTE_STOKES_PTOTAL);
    TestCubeHistogram(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, COMPUTE_STOKES_PFTOTAL);
    TestCubeHistogram(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, COMPUTE_STOKES_PLINEAR);
    TestCubeHistogram(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, COMPUTE_STOKES_PFLINEAR);
    TestCubeHistogram(FileFinder::Hdf5ImagePath(SAMPLE_IMAGE_HDF5), 0, COMPUTE_STOKES_PANGLE);
}
