/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "ImageGenerators/ImageMoments.h"
#include "ImageGenerators/MomentCalculator.h"
#include "Logger/Logger.h"
#include "Timer/Timer.h"

#include <casacore/images/Images/PagedImage.h>
#include <imageanalysis/ImageAnalysis/ImageMoments.h>

#include "CommonTestUtilities.h"

using namespace carta;

class MomentTest : public ::testing::Test, public FileFinder {
public:
    static void GetImageData(
        std::shared_ptr<const casacore::ImageInterface<casacore::Float>> image, std::vector<float>& data, int stokes = 0) {
        // Get spectral and stokes indices
        casacore::CoordinateSystem coord_sys = image->coordinates();
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

    static void CompareImageData(std::shared_ptr<const casacore::ImageInterface<casacore::Float>> image1,
        std::shared_ptr<const casacore::ImageInterface<casacore::Float>> image2) {
        std::vector<float> data1;
        std::vector<float> data2;
        GetImageData(image1, data1);
        GetImageData(image2, data2);
        CmpVectors(data1, data2);
    }

    static void CmpVectorsRatio(const std::vector<float>& data1, const std::vector<float>& data2, float abs_err) {
        EXPECT_EQ(data1.size(), data2.size());
        if (data1.size() == data2.size()) {
            for (int i = 0; i < data1.size(); ++i) {
                if (!std::isnan(data1[i]) || !std::isnan(data2[i])) {
                    if (std::abs(data1[i]) >= 1.0 || std::abs(data2[i]) >= 1.0) {
                        float diff = std::fabs(data1[i] - data2[i]);
                        float ratio1 = diff / std::abs(data1[i]);
                        float ratio2 = diff / std::abs(data2[i]);
                        EXPECT_LT(ratio1, abs_err);
                        EXPECT_LT(ratio2, abs_err);
                    } else {
                        EXPECT_NEAR(data1[i], data2[i], abs_err);
                    }
                }
            }
        }
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

    static std::vector<std::shared_ptr<casacore::ImageInterface<float>>> GenerateMoments(
        const std::shared_ptr<casacore::ImageInterface<float>>& image, int moments_axis, const std::vector<float>& include_pix,
        const std::vector<float>& exclude_pix, const vector<int>& moment_types) {
        // create carta moments generators
        casacore::LogOrigin carta_log("carta::ImageMoment", "createMoments", WHERE);
        casacore::LogIO carta_os(carta_log);
        carta::ImageMoments<float> carta_image_moments(*image, carta_os, nullptr, true);

        // the other settings
        casacore::Bool do_temp(true), remove_axis(false);
        casacore::Vector<float> casa_include_pix = include_pix;
        casacore::Vector<float> casa_exclude_pix = exclude_pix;

        // calculate moments with carta moment generator
        carta_image_moments.setMoments(moment_types);
        carta_image_moments.setMomentAxis(moments_axis);
        carta_image_moments.setInExCludeRange(casa_include_pix, casa_exclude_pix);
        auto moment_images = carta_image_moments.createMoments(do_temp, "carta_image_moments", remove_axis);

        std::vector<std::shared_ptr<casacore::ImageInterface<float>>> results;
        for (int i = 0; i < moment_images.size(); ++i) {
            results.push_back(dynamic_pointer_cast<casacore::ImageInterface<casacore::Float>>(moment_images[i]));
        }
        return results;
    }

    static void TestImageMoment(std::string filename, const std::vector<float>& include_pix, const std::vector<float>& exclude_pix) {
        std::shared_ptr<casacore::ImageInterface<float>> image;
        if (OpenImage(image, filename)) {
            // Set moment requests
            int moment_axis(2);
            std::vector<int> moment_types = {0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12};

            // Get image data
            std::vector<float> image_data;
            GetImageData(image, image_data);

            // First way to get image moments
            Timer t1;
            auto moment_calculator = carta::MomentCalculator(image);
            moment_calculator.SetMomentTypes(moment_types);
            moment_calculator.SetInExcludeRange(include_pix, exclude_pix);
            auto moment_images1 = moment_calculator.CreateMoments(image_data.data(), moment_axis);
            fmt::print("Elapsed time for calculating moment images (new) {:.3f} ms\n", t1.Elapsed().ms());

            // Second way to get image moments, through the Carta moment generator
            Timer t2;
            auto moment_images2 = GenerateMoments(image, moment_axis, include_pix, exclude_pix, moment_types);
            fmt::print("Elapsed time for calculating moment images (old) {:.3f} ms\n", t2.Elapsed().ms());

            // Check the consistency of two ways
            for (int i = 0; i < moment_images2.size(); ++i) {
                std::vector<float> results1;
                GetImageData(moment_images1[i], results1);

                std::vector<float> results2;
                GetImageData(moment_images2[i], results2);

                if (moment_types[i] == 3) {
                    CmpVectorsRatio(results1, results2, 1.0e-2);
                } else {
                    CmpVectors(results1, results2, 1.0e-6);
                }
            }
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
    std::string filename = FitsImagePath("M17_SWex_unittest.fits");
    float delta_pixel_range(1.0e-3);

    // No pixel range requirements
    TestImageMoment(filename, {}, {});

    // Pixel range inclusive
    TestImageMoment(filename, {delta_pixel_range}, {});
    TestImageMoment(filename, {-delta_pixel_range, delta_pixel_range}, {});

    // Pixel range exclusive
    TestImageMoment(filename, {}, {delta_pixel_range});
    TestImageMoment(filename, {}, {-delta_pixel_range, delta_pixel_range});
}
