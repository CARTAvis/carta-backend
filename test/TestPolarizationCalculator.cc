/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include <casacore/images/Images/PagedImage.h>
#include <imageanalysis/ImageAnalysis/ImageMoments.h>

#include "CommonTestUtilities.h"
#include "ImageData/PolarizationCalculator.h"
#include "Logger/Logger.h"

static const string SAMPLE_IMAGE = "IRCp10216_sci.spw0.cube.IQUV.manual.pbcor.fits"; // shape: [256, 256, 480, 4]

using namespace carta;

class PolarizationCalculatorTest : public ::testing::Test, public FileFinder {
public:
    static void GetImageData(
        std::shared_ptr<const casacore::ImageInterface<casacore::Float>> image, int channel, int stokes, std::vector<float>& data) {
        // Get spectral and stokes indices
        casacore::CoordinateSystem coord_sys = image->coordinates();
        casacore::Vector<casacore::Int> linear_axes = coord_sys.linearAxesNumbers();
        int spectral_axis = coord_sys.spectralAxisNumber();
        int stokes_axis = coord_sys.polarizationAxisNumber();
        spdlog::debug("spectral axis = {}, stokes axis = {}", spectral_axis, stokes_axis);

        // Get a slicer
        casacore::IPosition start(image->shape().size());
        start = 0;
        casacore::IPosition end(image->shape());
        end -= 1;

        auto spectral_axis_size = image->shape()[spectral_axis];
        if ((spectral_axis >= 0) && (channel >= spectral_axis_size)) {
            spdlog::error("channel number {} is greater or equal than the spectral axis size {}", channel, spectral_axis_size);
            return;
        }

        auto stokes_axis_size = image->shape()[stokes_axis];
        if ((stokes_axis >= 0) && (stokes >= stokes_axis_size)) {
            spdlog::error("stokes number {} is greater or equal than the stokes axis size {}", stokes, stokes_axis_size);
            return;
        }

        spdlog::debug("spectral axis size = {}, stokes axis size = {}", spectral_axis_size, stokes_axis_size);

        if (spectral_axis >= 0) {
            start(spectral_axis) = channel;
            end(spectral_axis) = channel;
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
            casacore::IPosition cursor_shape(lattice_iter.cursorShape());
            casacore::IPosition cursor_position(lattice_iter.position());
            casacore::Slicer cursor_slicer(cursor_position, cursor_shape); // where to put the data
            tmp(cursor_slicer) = cursor_data;
        }
    }

    static void GeneratePolarizedIntensity(const std::shared_ptr<casacore::ImageInterface<float>>& image) {
        // Calculate polarized intensity
        carta::PolarizationCalculator polarization_calculator(image);
        auto resulting_image = polarization_calculator.ComputePolarizationIntensity();

        // Get spectral axis size
        casacore::CoordinateSystem coord_sys = resulting_image->coordinates();
        int spectral_axis = coord_sys.spectralAxisNumber();
        int spectral_axis_size = image->shape()[spectral_axis];

        // Verify each pixel value from calculation results
        int stokes_q(1);
        int stokes_u(2);
        std::vector<float> data_q;
        std::vector<float> data_u;
        std::vector<float> data_results;

        for (int channel = 0; channel < spectral_axis_size; ++channel) {
            GetImageData(image, channel, stokes_q, data_q);
            GetImageData(image, channel, stokes_u, data_u);
            GetImageData(resulting_image, channel, 0, data_results);

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

    static void GenerateFractionalPolarizedIntensity(const std::shared_ptr<casacore::ImageInterface<float>>& image) {
        // Calculate polarized intensity
        carta::PolarizationCalculator polarization_calculator(image);
        auto resulting_image = polarization_calculator.ComputeFractionalPolarizationIntensity();

        // Get spectral axis size
        casacore::CoordinateSystem coord_sys = resulting_image->coordinates();
        int spectral_axis = coord_sys.spectralAxisNumber();
        int spectral_axis_size = image->shape()[spectral_axis];

        // Verify each pixel value from calculation results
        int stokes_i(0);
        int stokes_q(1);
        int stokes_u(2);
        std::vector<float> data_i;
        std::vector<float> data_q;
        std::vector<float> data_u;
        std::vector<float> data_results;

        for (int channel = 0; channel < spectral_axis_size; ++channel) {
            GetImageData(image, channel, stokes_i, data_i);
            GetImageData(image, channel, stokes_q, data_q);
            GetImageData(image, channel, stokes_u, data_u);
            GetImageData(resulting_image, channel, 0, data_results);

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

    static void GeneratePolarizationAngle(const std::shared_ptr<casacore::ImageInterface<float>>& image, bool radiant) {
        carta::PolarizationCalculator polarization_calculator(image);
        // Calculate polarized angle
        auto resulting_image = polarization_calculator.ComputePolarizationAngle(radiant);

        // Get spectral axis size
        casacore::CoordinateSystem coord_sys = resulting_image->coordinates();
        int spectral_axis = coord_sys.spectralAxisNumber();
        int spectral_axis_size = image->shape()[spectral_axis];

        // Verify each pixel value from calculation results
        int stokes_q(1);
        int stokes_u(2);
        std::vector<float> data_q;
        std::vector<float> data_u;
        std::vector<float> data_results;

        for (int channel = 0; channel < spectral_axis_size; ++channel) {
            GetImageData(image, channel, stokes_q, data_q);
            GetImageData(image, channel, stokes_u, data_u);
            GetImageData(resulting_image, channel, 0, data_results);

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
};

TEST_F(PolarizationCalculatorTest, PolarizedIntensity) {
    std::string file_path = FitsImagePath(SAMPLE_IMAGE);
    std::shared_ptr<casacore::ImageInterface<float>> image;

    if (OpenImage(image, file_path)) {
        GeneratePolarizedIntensity(image);
    } else {
        spdlog::warn("Fail to open the file {}! Ignore the CheckPolarizedIntensity test.", file_path);
    }
}

TEST_F(PolarizationCalculatorTest, FractionalPolarizedIntensity) {
    std::string file_path = FitsImagePath(SAMPLE_IMAGE);
    std::shared_ptr<casacore::ImageInterface<float>> image;

    if (OpenImage(image, file_path)) {
        GenerateFractionalPolarizedIntensity(image);
    } else {
        spdlog::warn("Fail to open the file {}! Ignore the CheckFractionalPolarizedIntensity test.", file_path);
    }
}

TEST_F(PolarizationCalculatorTest, PolarizedAngle) {
    std::string file_path = FitsImagePath(SAMPLE_IMAGE);
    std::shared_ptr<casacore::ImageInterface<float>> image;

    if (OpenImage(image, file_path)) {
        GeneratePolarizationAngle(image, true);
        GeneratePolarizationAngle(image, false);
    } else {
        spdlog::warn("Fail to open the file {}! Ignore the CheckPolarizedAngle test.", file_path);
    }
}
