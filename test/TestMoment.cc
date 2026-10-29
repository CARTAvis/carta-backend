/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "ImageGenerators/ImageMoments.h"
#include "ImageGenerators/MomentGenerator.h"
#include "Logger/Logger.h"

#include <casacore/coordinates/Coordinates/CoordinateUtil.h>
#include <casacore/images/Images/PagedImage.h>
#include <casacore/images/Images/TempImage.h>
#include <casacore/lattices/LRegions/LCBox.h>
#include <imageanalysis/ImageAnalysis/ImageMoments.h>

#include "CommonTestUtilities.h"

using namespace carta;

class MomentTest : public ::testing::Test {
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
        EXPECT_EQ(data1.size(), data2.size());

        if (data1.size() == data2.size()) {
            for (int i = 0; i < data1.size(); ++i) {
                EXPECT_FLOAT_EQ(data1[i], data2[i]);
            }
        }
    }

    static void GenerateMoments(const std::shared_ptr<casacore::ImageInterface<float>>& image, int moments_axis) {
        ASSERT_TRUE(image) << "Image must not be null";
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

        ASSERT_LT(moments_axis, image->shape().size()) << "Moment axis out of range for image shape";

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

            ASSERT_TRUE(casa_moment_image) << "CASA moment image is null at index " << i;
            ASSERT_TRUE(carta_moment_image) << "CARTA moment image is null at index " << i;

            EXPECT_EQ(casa_moment_image->shape().size(), carta_moment_image->shape().size());
            CompareImageData(casa_moment_image, carta_moment_image);
        }
    }
};

TEST_F(MomentTest, CheckConsistency) {
    auto file_path = FitsImages() / "M17_SWex_unittest.fits";
    std::shared_ptr<casacore::ImageInterface<float>> image = std::make_shared<casacore::FITSImage>(file_path.string());
    int moment_axis(2);

    GenerateMoments(image, moment_axis);
}

TEST_F(MomentTest, CheckConsistencyForBeamConvolutions) {
    auto file_path = FitsImages() / "small_perplanebeam.fits";
    std::shared_ptr<casacore::ImageInterface<float>> image = std::make_shared<casacore::FITSImage>(file_path.string());
    int moment_axis(2);

    GenerateMoments(image, moment_axis);
}

TEST_F(MomentTest, ComputesSpectralRangesFromChannels) {
    const casacore::IPosition shape(3, 2, 2, 4);
    auto coordinates = casacore::CoordinateUtil::defaultCoords3D();
    casacore::SpectralCoordinate spectral_coordinate(casacore::MFrequency::LSRK, 100e9, 1e9, 0, 230538000000.0);
    coordinates.replaceCoordinate(spectral_coordinate, coordinates.findCoordinate(casacore::Coordinate::SPECTRAL));
    auto image = std::make_shared<casacore::TempImage<float>>(casacore::TiledShape(shape), coordinates);
    image->set(1.0f);

    CARTA::MomentRequest request;
    request.set_axis(CARTA::MomentAxis::SPECTRAL);
    request.add_moments(CARTA::Moment::MEAN_OF_THE_SPECTRUM);
    request.mutable_spectral_range()->set_min(0);
    request.mutable_spectral_range()->set_max(3);
    request.set_rest_freq(230538000000.0);
    request.set_spectral_system("LSRK");

    RegionState region;
    region.type = CARTA::RegionType::RECTANGLE;
    region.control_points.resize(2);
    region.control_points[0].set_x(0.5f);
    region.control_points[0].set_y(0.5f);
    region.control_points[1].set_x(2);
    region.control_points[1].set_y(2);

    MomentGenerator generator("test.fits", image);
    CARTA::MomentResponse response;
    std::vector<GeneratedImage> results;
    const casacore::ImageRegion image_region{casacore::LCBox(shape)};
    ASSERT_TRUE(generator.CalculateMoments(
        0, image_region, coordinates.spectralAxisNumber(), -1, 0, [](float) {}, request, response, results, region, "I"))
        << response.message();
    ASSERT_EQ(results.size(), 1);

    std::string history;
    const auto& logger = results[0].image->logger();
    for (auto iter = logger.begin(); iter != logger.end(); ++iter) {
        history += iter->message();
    }
    EXPECT_NE(history.find("System: LSRK"), std::string::npos);
    EXPECT_NE(history.find("Rest Frequency: 230.538 GHz"), std::string::npos);
    EXPECT_NE(history.find("Channel range: [0, 3]\n"), std::string::npos);
    EXPECT_NE(history.find("Frequency range: [100.0000, 103.0000](GHz)\n"), std::string::npos);
    const double c_velocity = 299792.458;
    const std::string velocity_unit = "km/s";
    EXPECT_NE(history.find(fmt::format("Radio velocity range: [{:.4f}, {:.4f}]({})\n", c_velocity * (1 - 100e9 / request.rest_freq()),
                  c_velocity * (1 - 103e9 / request.rest_freq()), velocity_unit)),
        std::string::npos);
    EXPECT_NE(history.find(fmt::format("Optical velocity range: [{:.4f}, {:.4f}]({})\n", c_velocity * (request.rest_freq() / 100e9 - 1),
                  c_velocity * (request.rest_freq() / 103e9 - 1), velocity_unit)),
        std::string::npos);
    EXPECT_NE(history.find("Vacuum wavelength range: [2.9979, 2.9106](mm)\n"), std::string::npos);
    EXPECT_NE(history.find("Air wavelength range: "), std::string::npos);
}
