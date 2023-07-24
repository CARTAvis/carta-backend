/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "ImageGenerators/ImageMoments.h"
#include "ImageGenerators/MomentCalculator.h"
#include "Logger/Logger.h"

#include <casacore/images/Images/PagedImage.h>
#include <imageanalysis/ImageAnalysis/ImageMoments.h>

#include "CommonTestUtilities.h"

using namespace carta;

class MomentTest : public ::testing::Test, public FileFinder {
public:
    static void GetImageData(std::shared_ptr<const casacore::ImageInterface<casacore::Float>> image, std::vector<float>& data) {
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

        if (spectral_axis >= 0) {
            start(spectral_axis) = 0;
            end(spectral_axis) = image->shape()[spectral_axis] - 1;
        }
        if (stokes_axis >= 0) {
            start(stokes_axis) = 0;
            end(stokes_axis) = 0;
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
                casacore::Array<float> masked_data(cursor_data);
                const casacore::Array<bool> cursor_mask = lattice_iter.getMask();
                bool del_mask_ptr;
                const bool* cursor_mask_ptr = cursor_mask.getStorage(del_mask_ptr);
                bool del_data_ptr;
                float* masked_data_ptr = masked_data.getStorage(del_data_ptr);
                for (size_t i = 0; i < cursor_data.nelements(); ++i) {
                    if (!cursor_mask_ptr[i]) {
                        masked_data_ptr[i] = NAN;
                    }
                }
                cursor_mask.freeStorage(cursor_mask_ptr, del_mask_ptr);
                masked_data.putStorage(masked_data_ptr, del_data_ptr);
            }
            casacore::IPosition cursor_shape(lattice_iter.cursorShape());
            casacore::IPosition cursor_position(lattice_iter.position());
            casacore::Slicer cursor_slicer(cursor_position, cursor_shape); // where to put the data
            tmp(cursor_slicer) = cursor_data;
        }
    }

    static void GetImageData(
        std::shared_ptr<const casacore::ImageInterface<casacore::Float>> image, int x, int y, std::vector<float>& data) {
        // Get spectral and stokes indices
        casacore::CoordinateSystem coord_sys = image->coordinates();
        int spectral_axis = coord_sys.spectralAxisNumber();
        int stokes_axis = coord_sys.polarizationAxisNumber();

        // Get a slicer
        casacore::IPosition start(image->shape().size());
        start = 0;
        casacore::IPosition end(image->shape());
        end -= 1;

        // x slice
        start(0) = x;
        end(0) = x;

        // y slice
        start(1) = y;
        end(1) = y;

        // spectral slice
        if (spectral_axis >= 0) {
            start(spectral_axis) = 0;
            end(spectral_axis) = image->shape()[spectral_axis] - 1;
        }

        // stokes slice
        if (stokes_axis >= 0) {
            start(stokes_axis) = 0;
            end(stokes_axis) = 0;
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
                casacore::Array<float> masked_data(cursor_data);
                const casacore::Array<bool> cursor_mask = lattice_iter.getMask();
                bool del_mask_ptr;
                const bool* cursor_mask_ptr = cursor_mask.getStorage(del_mask_ptr);
                bool del_data_ptr;
                float* masked_data_ptr = masked_data.getStorage(del_data_ptr);
                for (size_t i = 0; i < cursor_data.nelements(); ++i) {
                    if (!cursor_mask_ptr[i]) {
                        masked_data_ptr[i] = NAN;
                    }
                }
                cursor_mask.freeStorage(cursor_mask_ptr, del_mask_ptr);
                masked_data.putStorage(masked_data_ptr, del_data_ptr);
            }
            casacore::IPosition cursor_shape(lattice_iter.cursorShape());
            casacore::IPosition cursor_position(lattice_iter.position());
            casacore::Slicer cursor_slicer(cursor_position, cursor_shape);
            tmp(cursor_slicer) = cursor_data;
        }
    }

    static void CompareImageData(std::shared_ptr<const casacore::ImageInterface<casacore::Float>> image1,
        std::shared_ptr<const casacore::ImageInterface<casacore::Float>> image2) {
        std::vector<float> data1;
        std::vector<float> data2;
        GetImageData(image1, data1);
        GetImageData(image2, data2);
        CmpVectors(data1, data2);
    }

    static void GenerateMoments(const std::shared_ptr<casacore::ImageInterface<float>>& image, int moments_axis) {
        // create casa/carta moments generators
        casacore::LogOrigin casa_log("casa::ImageMoment", "createMoments", WHERE);
        casacore::LogIO casa_os(casa_log);
        casacore::LogOrigin carta_log("carta::ImageMoment", "createMoments", WHERE);
        casacore::LogIO carta_os(carta_log);
        casa::ImageMoments<float> casa_image_moments(*image, casa_os, true);
        carta::ImageMoments<float> carta_image_moments(*image, carta_os, nullptr, true);

        // set moment types
        casacore::Vector<casacore::Int> moments(12);
        moments[0] = 0;   // AVERAGE
        moments[1] = 1;   // INTEGRATED
        moments[2] = 2;   // WEIGHTED_MEAN_COORDINATE
        moments[3] = 3;   // WEIGHTED_DISPERSION_COORDINATE
        moments[4] = 4;   // MEDIAN
        moments[5] = 6;   // STANDARD_DEVIATION
        moments[6] = 7;   // RMS
        moments[7] = 8;   // ABS_MEAN_DEVIATION
        moments[8] = 9;   // MAXIMUM
        moments[9] = 10;  // MAXIMUM_COORDINATE
        moments[10] = 11; // MINIMUM
        moments[11] = 12; // MINIMUM_COORDINATE

        // the other settings
        casacore::Vector<float> include_pix;
        casacore::Vector<float> exclude_pix;
        casacore::Bool do_temp(true);
        casacore::Bool remove_axis(false);

        // calculate moments with casa moment generator
        casa_image_moments.setMoments(moments);
        casa_image_moments.setMomentAxis(moments_axis);
        casa_image_moments.setInExCludeRange(include_pix, exclude_pix);
        auto casa_results = casa_image_moments.createMoments(do_temp, "casa_image_moments", remove_axis);

        // calculate moments with carta moment generator
        carta_image_moments.setMoments(moments);
        carta_image_moments.setMomentAxis(moments_axis);
        carta_image_moments.setInExCludeRange(include_pix, exclude_pix);
        auto carta_results = carta_image_moments.createMoments(do_temp, "carta_image_moments", remove_axis);

        // check the consistency of casa/carta results
        EXPECT_EQ(casa_results.size(), carta_results.size());
        EXPECT_EQ(carta_results.size(), moments.size());

        for (int i = 0; i < casa_results.size(); ++i) {
            auto casa_moment_image = dynamic_pointer_cast<casacore::ImageInterface<casacore::Float>>(casa_results[i]);
            auto carta_moment_image = dynamic_pointer_cast<casacore::ImageInterface<casacore::Float>>(carta_results[i]);

            EXPECT_EQ(casa_moment_image->shape().size(), carta_moment_image->shape().size());
            CompareImageData(casa_moment_image, carta_moment_image);
        }
    }

    static std::shared_ptr<casacore::ImageInterface<float>> GenerateMoments(
        const std::shared_ptr<casacore::ImageInterface<float>>& image, int moments_axis, casacore::Int moment_type) {
        // create carta moments generators
        casacore::LogOrigin carta_log("carta::ImageMoment", "createMoments", WHERE);
        casacore::LogIO carta_os(carta_log);
        carta::ImageMoments<float> carta_image_moments(*image, carta_os, nullptr, true);

        // set moment types
        casacore::Vector<casacore::Int> moments = {moment_type};

        // the other settings
        casacore::Vector<float> include_pix;
        casacore::Vector<float> exclude_pix;
        casacore::Bool do_temp(true);
        casacore::Bool remove_axis(false);

        // calculate moments with carta moment generator
        carta_image_moments.setMoments(moments);
        carta_image_moments.setMomentAxis(moments_axis);
        carta_image_moments.setInExCludeRange(include_pix, exclude_pix);
        auto results = carta_image_moments.createMoments(do_temp, "carta_image_moments", remove_axis);

        return dynamic_pointer_cast<casacore::ImageInterface<casacore::Float>>(results[0]);
    }

    static void TestImageMoment(int moment_type, float error = 0.0) {
        std::string file_path = FitsImagePath("M17_SWex_unittest.fits");
        std::shared_ptr<casacore::ImageInterface<float>> image;

        if (OpenImage(image, file_path)) {
            auto shape = image->shape();

            // Moment requirements
            int moment_axis(2);

            // Carta moment calculator
            std::vector<int> moment_types = {moment_type};
            auto moment_calculator = carta::MomentCalculator(image, moment_types);
            std::vector<float> moment_results1; // first way results

            for (int x = 0; x < shape(0); ++x) {
                for (int y = 0; y < shape(1); ++y) {
                    std::vector<float> data;
                    GetImageData(image, x, y, data);

                    // Do calculations
                    std::unordered_map<int, float> results;
                    moment_calculator.DoCalculation(data.data(), data.size(), results);
                    if (results.count(moment_type)) {
                        moment_results1.push_back(results[moment_type]);
                    }
                }
            }

            // Carta moment generator
            auto moment_image = GenerateMoments(image, moment_axis, moment_type);
            auto moment_shape = moment_image->shape();

            std::vector<float> moment_results2; // second way results
            for (int x = 0; x < moment_shape(0); ++x) {
                for (int y = 0; y < moment_shape(1); ++y) {
                    std::vector<float> data;
                    GetImageData(moment_image, x, y, data);
                    moment_results2.push_back(data[0]);
                }
            }

            // Check the consistency of two calculation results
            CmpVectors(moment_results1, moment_results2, error);
        }
    }
};

TEST_F(MomentTest, CheckConsistency) {
    std::string file_path = FitsImagePath("M17_SWex_unittest.fits");
    std::shared_ptr<casacore::ImageInterface<float>> image;
    int moment_axis(2);

    if (OpenImage(image, file_path)) {
        GenerateMoments(image, moment_axis);
    } else {
        spdlog::warn("Fail to open the file {}! Ignore the Moment test.", file_path);
    }
}

TEST_F(MomentTest, CheckConsistencyForBeamConvolutions) {
    std::string file_path = FitsImagePath("small_perplanebeam.fits");
    std::shared_ptr<casacore::ImageInterface<float>> image;
    int moment_axis(2);

    if (OpenImage(image, file_path)) {
        GenerateMoments(image, moment_axis);
    } else {
        spdlog::warn("Fail to open the file {}! Ignore the Moment test.", file_path);
    }
}

TEST_F(MomentTest, TestMomentCalculator) {
    TestImageMoment(0);
    TestImageMoment(1);
    TestImageMoment(2);
    TestImageMoment(3, 1e-2);
}
