/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "ImageGenerators/ImageMoments.h"
#include "Logger/Logger.h"
#include "src/Frame/Frame.h"

#include <casacore/images/Images/PagedImage.h>
#include <imageanalysis/ImageAnalysis/ImageMoments.h>

#include "CommonTestUtilities.h"

using namespace carta;

// Allows testing of protected methods in Frame without polluting the original class
class TestFrame : public Frame {
public:
    TestFrame(uint32_t session_id, std::shared_ptr<carta::FileLoader> loader, const std::string& hdu, int default_z = DEFAULT_Z)
        : Frame(session_id, loader, hdu, default_z) {}
    bool SaveImage(casacore::ImageInterface<casacore::Float>& image, fs::path output_path) {
        casacore::String message;
        return ExportFITSImage(image, output_path, message);
    }
};

// TODO do we actually need to support paged images in this test?
class MomentTest : public ::testing::Test, public FileFinder {
public:
    static void OpenImage(std::shared_ptr<casacore::ImageInterface<float>>& image, const std::string& filename, uInt hdu_num = 0) {
        casacore::ImageOpener::ImageTypes image_types = casacore::ImageOpener::imageType(filename);
        switch (image_types) {
            case casacore::ImageOpener::AIPSPP:
                image = std::make_shared<casacore::PagedImage<float>>(filename);
                break;
            case casacore::ImageOpener::FITS:
                image = std::make_shared<casacore::FITSImage>(filename, 0, hdu_num);
                break;
            default:
                throw casacore::AipsError(fmt::format("Could not open test file {}", filename));
        }
    }

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

    // TODO when we switch to our own moments implementation, this will be moved to an external utility so that we can eliminate our
    // dependency on imageanalysis
    static void SaveCasaMoments(std::string image_name, int moments_axis, casacore::Vector<casacore::Int> moments,
        casacore::Vector<float> include_pix, casacore::Vector<float> exclude_pix, casacore::Bool do_temp, casacore::Bool remove_axis) {
        std::string file_path = FitsImagePath(image_name);
        std::shared_ptr<casacore::ImageInterface<float>> image;
        OpenImage(image, file_path);

        // create casa moments generator
        casacore::LogOrigin casa_log("casa::ImageMoment", "createMoments", WHERE);
        casacore::LogIO casa_os(casa_log);
        casa::ImageMoments<float> casa_image_moments(*image, casa_os, true);

        // calculate moments with casa moment generator
        casa_image_moments.setMoments(moments);
        casa_image_moments.setMomentAxis(moments_axis);
        casa_image_moments.setInExCludeRange(include_pix, exclude_pix);
        auto casa_results = casa_image_moments.createMoments(do_temp, "casa_image_moments", remove_axis);

        // dummy frame for saving images
        std::unique_ptr<TestFrame> frame(new TestFrame(0, nullptr, "0"));

        for (int i = 0; i < casa_results.size(); ++i) {
            auto casa_moment_image = dynamic_pointer_cast<casacore::ImageInterface<casacore::Float>>(casa_results[i]);
            std::string base_filename = fs::path(image_name).stem().string();
            auto output_filename = TestRoot() / "data" / "images" / "fits" / fmt::format("{}_moment_{}_casa.fits", base_filename, i);
            frame->SaveImage(*casa_moment_image, output_filename);
        }
    }

    static void CheckCartaMoments(std::string image_name, int moments_axis, casacore::Vector<casacore::Int> moments,
        casacore::Vector<float> include_pix, casacore::Vector<float> exclude_pix, casacore::Bool do_temp, casacore::Bool remove_axis) {
        std::string file_path = FitsImagePath(image_name);
        std::shared_ptr<casacore::ImageInterface<float>> image;
        OpenImage(image, file_path);

        casacore::LogOrigin carta_log("carta::ImageMoment", "createMoments", WHERE);
        casacore::LogIO carta_os(carta_log);
        carta::ImageMoments<float> carta_image_moments(*image, carta_os, nullptr, true);

        carta_image_moments.setMoments(moments);
        carta_image_moments.setMomentAxis(moments_axis);
        carta_image_moments.setInExCludeRange(include_pix, exclude_pix);
        auto carta_results = carta_image_moments.createMoments(do_temp, "carta_image_moments", remove_axis);

        EXPECT_EQ(carta_results.size(), moments.size());

        // dummy frame for saving images
        std::unique_ptr<TestFrame> frame(new TestFrame(0, nullptr, "0"));

        for (int i = 0; i < carta_results.size(); ++i) {
            std::string base_filename = fs::path(image_name).stem().string();
            std::string casa_moment_image_path = FitsImagePath(fmt::format("{}_moment_{}_casa.fits", base_filename, i));
            std::shared_ptr<casacore::ImageInterface<float>> casa_moment_image;
            OpenImage(casa_moment_image, casa_moment_image_path);

            auto carta_moment_image = dynamic_pointer_cast<casacore::ImageInterface<casacore::Float>>(carta_results[i]);
            auto output_filename = TestRoot() / "data" / "images" / "generated" / fmt::format("{}_moment_{}_carta.fits", base_filename, i);
            frame->SaveImage(*carta_moment_image, output_filename);

            OpenImage(carta_moment_image, output_filename);

            EXPECT_EQ(casa_moment_image->shape().size(), carta_moment_image->shape().size());
            CompareImageData(casa_moment_image, carta_moment_image);
        }
    }
};

TEST_F(MomentTest, CheckConsistency) {
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

    casacore::Vector<float> include_pix;
    casacore::Vector<float> exclude_pix;
    casacore::Bool do_temp(true);
    casacore::Bool remove_axis(false);

    SaveCasaMoments("M17_SWex_unittest.fits", 2, moments, include_pix, exclude_pix, do_temp, remove_axis);
    CheckCartaMoments("M17_SWex_unittest.fits", 2, moments, include_pix, exclude_pix, do_temp, remove_axis);
}

TEST_F(MomentTest, CheckConsistencyForBeamConvolutions) {
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

    casacore::Vector<float> include_pix;
    casacore::Vector<float> exclude_pix;
    casacore::Bool do_temp(true);
    casacore::Bool remove_axis(false);

    SaveCasaMoments("small_perplanebeam.fits", 2, moments, include_pix, exclude_pix, do_temp, remove_axis);
    CheckCartaMoments("small_perplanebeam.fits", 2, moments, include_pix, exclude_pix, do_temp, remove_axis);
}
