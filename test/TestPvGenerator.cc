/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include "CommonTestUtilities.h"
#include "ImageData/FileLoader.h"
#include "ImageGenerators/ImageGenerator.h"
#include "Region/Region.h"
#include "Region/RegionHandler.h"
#include "src/Frame/Frame.h"

using namespace carta;

using ::testing::FloatNear;
using ::testing::Pointwise;

class PvGeneratorTest : public ::testing::Test, public ImageGenerator {
public:
    static void SetPvCut(carta::RegionHandler& region_handler, int file_id, int& region_id, std::vector<float>& endpoints,
        std::shared_ptr<casacore::CoordinateSystem> csys) {
        // Define RegionState for line region
        std::vector<CARTA::Point> control_points;
        control_points.push_back(Message::Point(endpoints[0], endpoints[1]));
        control_points.push_back(Message::Point(endpoints[2], endpoints[3]));
        RegionState region_state(file_id, CARTA::RegionType::LINE, control_points, 0.0);

        // Set region
        region_handler.SetRegion(region_id, region_state, csys);
    }

    void SetUp() {
        setenv("HDF5_USE_FILE_LOCKING", "FALSE", 0);
    }

    static void TestAveragingWidthRange(int width, bool expected_width_range) {
        auto image_path = TestRoot() / "data/images/fits/noise_3d.fits"; // 10x10x10 image
        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(image_path));
        std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));
        carta::RegionHandler region_handler;
        int file_id(0), region_id(-1);
        std::vector<float> endpoints = {0.0, 0.0, 9.0, 9.0}; // Set line region [0, 0] to [9, 9]
        SetPvCut(region_handler, file_id, region_id, endpoints, frame->CoordinateSystem());

        // Request PV image
        auto pv_request = Message::PvRequest(file_id, region_id, width);
        auto progress_callback = [&](float progress) {};
        CARTA::PvResponse pv_response;
        carta::GeneratedImage pv_image;
        region_handler.CalculatePvImage(pv_request, frame, progress_callback, pv_response, pv_image);

        if (expected_width_range) {
            EXPECT_TRUE(pv_response.success());
            EXPECT_FALSE(pv_response.cancel());
            EXPECT_NE(pv_image.image.get(), nullptr);
            EXPECT_TRUE(pv_response.message().empty());
        } else {
            EXPECT_FALSE(pv_response.success());
            EXPECT_FALSE(pv_response.cancel());
            EXPECT_EQ(pv_image.image.get(), nullptr);
            EXPECT_FALSE(pv_response.message().empty());
        }
    }
};

TEST_F(PvGeneratorTest, FitsPvImage) {
    auto image_path = TestRoot() / "data/images/fits/noise_3d.fits"; // 10x10x10 image
    std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(image_path));
    std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));

    // Image coordinate system
    auto csys = frame->CoordinateSystem();
    int image_spectral_axis = csys->spectralAxisNumber();
    auto image_spectral_name = csys->worldAxisNames()(image_spectral_axis);
    auto image_axis_increment = csys->increment();
    auto image_axis_units = csys->worldAxisUnits();
    auto image_cdelt2 = casacore::Quantity(image_axis_increment(1), image_axis_units(1));
    auto image_cdelt2_arcmin = image_cdelt2.get("arcmin").getValue();

    // Set line region [0, 0] to [9, 9]
    carta::RegionHandler region_handler;
    int file_id(0), region_id(-1);
    std::vector<float> endpoints = {0.0, 0.0, 9.0, 9.0};
    SetPvCut(region_handler, file_id, region_id, endpoints, csys);

    // Request PV image
    int width(3);
    auto pv_request = Message::PvRequest(file_id, region_id, width);
    auto progress_callback = [&](float progress) {};
    CARTA::PvResponse pv_response;
    carta::GeneratedImage pv_image;
    region_handler.CalculatePvImage(pv_request, frame, progress_callback, pv_response, pv_image);

    EXPECT_EQ(pv_response.success(), true);
    EXPECT_EQ(pv_response.cancel(), false);
    EXPECT_NE(pv_image.image.get(), nullptr);

    // Check PV coordinate system
    auto pv_coord_sys = pv_image.image->coordinates();
    EXPECT_EQ(pv_coord_sys.nCoordinates(), 2);
    EXPECT_EQ(pv_coord_sys.hasLinearCoordinate(), true);
    EXPECT_EQ(pv_coord_sys.hasSpectralAxis(), true);

    auto pv_linear_axes = pv_coord_sys.linearAxesNumbers();
    int pv_spectral_axis(pv_coord_sys.spectralAxisNumber());
    EXPECT_EQ(pv_linear_axes.size(), 2);
    EXPECT_EQ(pv_linear_axes(0), 0);
    EXPECT_EQ(pv_linear_axes(1), -1);
    EXPECT_EQ(pv_spectral_axis, 1);

    auto pv_linear_axis = pv_linear_axes(0);
    auto pv_axis_names = pv_coord_sys.worldAxisNames();
    auto pv_increment = pv_coord_sys.increment();
    auto pv_reference_value = pv_coord_sys.referenceValue();
    auto pv_axis_units = pv_coord_sys.worldAxisUnits();

    // Check linear (P) axis
    EXPECT_EQ(pv_axis_names(pv_linear_axis), "Offset");
    EXPECT_EQ(pv_axis_units(pv_linear_axis), "arcmin");
    EXPECT_FLOAT_EQ(pv_increment(pv_linear_axis), image_cdelt2_arcmin);
    EXPECT_EQ(pv_reference_value(pv_linear_axis), 0.0);

    // Check spectral (V) axis
    EXPECT_EQ(pv_axis_names(pv_spectral_axis), image_spectral_name);
    EXPECT_EQ(pv_increment(pv_spectral_axis), image_axis_increment(image_spectral_axis));
    EXPECT_EQ(pv_axis_units(pv_spectral_axis), image_axis_units(image_spectral_axis));

    // Check data
    casacore::Array<float> pv_data;
    pv_image.image->get(pv_data);
    EXPECT_EQ(pv_data.shape().size(), 2);
    EXPECT_EQ(pv_data.shape()(1), frame->Depth());
    EXPECT_FALSE(allEQ(pv_data, NAN));
}

TEST_F(PvGeneratorTest, FitsPvImageHorizontalCut) {
    auto image_path = TestRoot() / "data/images/fits/noise_3d.fits"; // 10x10x10 image
    std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(image_path));
    std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));

    // Image coordinate system
    auto csys = frame->CoordinateSystem();
    int image_spectral_axis = csys->spectralAxisNumber();
    auto image_spectral_name = csys->worldAxisNames()(image_spectral_axis);
    auto image_axis_increment = csys->increment();
    auto image_axis_units = csys->worldAxisUnits();
    auto image_cdelt2 = casacore::Quantity(image_axis_increment(1), image_axis_units(1));
    auto image_cdelt2_arcsec = image_cdelt2.get("arcsec").getValue();

    // Set line region at y=5
    carta::RegionHandler region_handler;
    int file_id(0), region_id(-1);
    std::vector<float> endpoints = {9.0, 5.0, 1.0, 5.0};
    SetPvCut(region_handler, file_id, region_id, endpoints, csys);

    // Request PV image
    int width(1);
    auto pv_request = Message::PvRequest(file_id, region_id, width);
    auto progress_callback = [&](float progress) {};
    CARTA::PvResponse pv_response;
    carta::GeneratedImage pv_image;
    region_handler.CalculatePvImage(pv_request, frame, progress_callback, pv_response, pv_image);

    EXPECT_EQ(pv_response.success(), true);
    EXPECT_EQ(pv_response.cancel(), false);
    EXPECT_NE(pv_image.image.get(), nullptr);

    // Check PV coordinate system
    auto pv_coord_sys = pv_image.image->coordinates();
    EXPECT_EQ(pv_coord_sys.nCoordinates(), 2);
    EXPECT_EQ(pv_coord_sys.hasLinearCoordinate(), true);
    EXPECT_EQ(pv_coord_sys.hasSpectralAxis(), true);

    auto pv_linear_axes = pv_coord_sys.linearAxesNumbers();
    int pv_spectral_axis(pv_coord_sys.spectralAxisNumber());
    EXPECT_EQ(pv_linear_axes.size(), 2);
    EXPECT_EQ(pv_linear_axes(0), 0);
    EXPECT_EQ(pv_linear_axes(1), -1);
    EXPECT_EQ(pv_spectral_axis, 1);

    auto pv_linear_axis = pv_linear_axes(0);
    auto pv_axis_names = pv_coord_sys.worldAxisNames();
    auto pv_increment = pv_coord_sys.increment();
    auto pv_reference_value = pv_coord_sys.referenceValue();
    auto pv_axis_units = pv_coord_sys.worldAxisUnits();

    // Check linear (P) axis
    EXPECT_EQ(pv_axis_names(pv_linear_axis), "Offset");
    EXPECT_EQ(pv_axis_units(pv_linear_axis), "arcsec");
    EXPECT_FLOAT_EQ(pv_increment(pv_linear_axis), image_cdelt2_arcsec);
    EXPECT_EQ(pv_reference_value(pv_linear_axis), 0.0);

    // Check spectral (V) axis
    EXPECT_EQ(pv_axis_names(pv_spectral_axis), image_spectral_name);
    EXPECT_EQ(pv_increment(pv_spectral_axis), image_axis_increment(image_spectral_axis));
    EXPECT_EQ(pv_axis_units(pv_spectral_axis), image_axis_units(image_spectral_axis));

    // Check data
    casacore::Array<float> pv_data;
    pv_image.image->get(pv_data);
    EXPECT_EQ(pv_data.shape().size(), 2);
    EXPECT_EQ(pv_data.shape()(0), 9);
    EXPECT_EQ(pv_data.shape()(1), frame->Depth());

    // Read image data slice
    FitsDataReader reader(image_path.string());
    auto image_data = reader.ReadRegion({1, 5, 0}, {10, 6, 10});

    EXPECT_EQ(pv_data.size(), image_data.size());
    EXPECT_THAT(pv_data.tovector(), Pointwise(FloatNear(1e-5), image_data));
}

TEST_F(PvGeneratorTest, FitsPvImageVerticalCut) {
    auto image_path = TestRoot() / "data/images/fits/noise_3d.fits"; // 10x10x10 image
    std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(image_path));
    std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));

    // Image coordinate system
    auto csys = frame->CoordinateSystem();
    int image_spectral_axis = csys->spectralAxisNumber();
    auto image_spectral_name = csys->worldAxisNames()(image_spectral_axis);
    auto image_axis_increment = csys->increment();
    auto image_axis_units = csys->worldAxisUnits();
    auto image_cdelt2 = casacore::Quantity(image_axis_increment(1), image_axis_units(1));
    auto image_cdelt2_arcsec = image_cdelt2.get("arcsec").getValue();

    // Set line region at y=5
    carta::RegionHandler region_handler;
    int file_id(0), region_id(-1);
    std::vector<float> endpoints = {5.0, 9.0, 5.0, 1.0};
    SetPvCut(region_handler, file_id, region_id, endpoints, csys);

    // Request PV image
    int width(1);
    auto pv_request = Message::PvRequest(file_id, region_id, width);
    auto progress_callback = [&](float progress) {};
    CARTA::PvResponse pv_response;
    carta::GeneratedImage pv_image;
    region_handler.CalculatePvImage(pv_request, frame, progress_callback, pv_response, pv_image);

    EXPECT_EQ(pv_response.success(), true);
    EXPECT_EQ(pv_response.cancel(), false);
    EXPECT_NE(pv_image.image.get(), nullptr);

    // Check PV coordinate system
    auto pv_coord_sys = pv_image.image->coordinates();
    EXPECT_EQ(pv_coord_sys.nCoordinates(), 2);
    EXPECT_EQ(pv_coord_sys.hasLinearCoordinate(), true);
    EXPECT_EQ(pv_coord_sys.hasSpectralAxis(), true);

    auto pv_linear_axes = pv_coord_sys.linearAxesNumbers();
    int pv_spectral_axis(pv_coord_sys.spectralAxisNumber());
    EXPECT_EQ(pv_linear_axes.size(), 2);
    EXPECT_EQ(pv_linear_axes(0), 0);
    EXPECT_EQ(pv_linear_axes(1), -1);
    EXPECT_EQ(pv_spectral_axis, 1);

    auto pv_linear_axis = pv_linear_axes(0);
    auto pv_axis_names = pv_coord_sys.worldAxisNames();
    auto pv_increment = pv_coord_sys.increment();
    auto pv_reference_value = pv_coord_sys.referenceValue();
    auto pv_axis_units = pv_coord_sys.worldAxisUnits();

    // Check linear (P) axis
    EXPECT_EQ(pv_axis_names(pv_linear_axis), "Offset");
    EXPECT_EQ(pv_axis_units(pv_linear_axis), "arcsec");
    EXPECT_FLOAT_EQ(pv_increment(pv_linear_axis), image_cdelt2_arcsec);
    EXPECT_EQ(pv_reference_value(pv_linear_axis), 0.0);

    // Check spectral (V) axis
    EXPECT_EQ(pv_axis_names(pv_spectral_axis), image_spectral_name);
    EXPECT_EQ(pv_increment(pv_spectral_axis), image_axis_increment(image_spectral_axis));
    EXPECT_EQ(pv_axis_units(pv_spectral_axis), image_axis_units(image_spectral_axis));

    // Check data
    casacore::Array<float> pv_data;
    pv_image.image->get(pv_data);
    EXPECT_EQ(pv_data.shape().size(), 2);
    EXPECT_EQ(pv_data.shape()(0), 9);
    EXPECT_EQ(pv_data.shape()(1), frame->Depth());

    // Read image data slice
    FitsDataReader reader(image_path.string());
    auto image_data = reader.ReadRegion({5, 1, 0}, {6, 10, 10});

    EXPECT_EQ(pv_data.size(), image_data.size());
    EXPECT_THAT(pv_data.tovector(), Pointwise(FloatNear(1e-5), image_data));
}

TEST_F(PvGeneratorTest, TestNoSpectralAxis) {
    auto image_path = TestRoot() / "data/images/hdf5/noise_10px_10px.hdf5";
    auto path_string = GeneratedHdf5ImagePath("10 10 10");
    std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
    std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));

    // Set line region [0, 0] to [9, 9]
    carta::RegionHandler region_handler;
    int file_id(0), region_id(-1);
    std::vector<float> endpoints = {0.0, 0.0, 9.0, 9.0};
    auto csys = frame->CoordinateSystem();
    SetPvCut(region_handler, file_id, region_id, endpoints, csys);

    // Request PV image
    int width(3);
    auto pv_request = Message::PvRequest(file_id, region_id, width);
    auto progress_callback = [&](float progress) {};
    CARTA::PvResponse pv_response;
    carta::GeneratedImage pv_image;
    region_handler.CalculatePvImage(pv_request, frame, progress_callback, pv_response, pv_image);

    EXPECT_EQ(pv_response.success(), false);
    EXPECT_EQ(pv_response.cancel(), false);
    EXPECT_EQ(pv_image.image.get(), nullptr);
}

TEST_F(PvGeneratorTest, AveragingWidthRange) {
    TestAveragingWidthRange(0, false);
    TestAveragingWidthRange(1, true);
    TestAveragingWidthRange(20, true);
    TestAveragingWidthRange(21, false);
}

TEST_F(PvGeneratorTest, PvImageSpectralRange) {
    // FITS
    auto image_path = TestRoot() / "data/images/fits/noise_3d.fits"; // 10x10x10 image
    std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(image_path));
    std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));
    auto csys = frame->CoordinateSystem();

    // Set line region [0, 0] to [9, 9]
    carta::RegionHandler region_handler;
    int file_id(0), region_id(-1);
    std::vector<float> endpoints = {0.0, 0.0, 9.0, 9.0};
    SetPvCut(region_handler, file_id, region_id, endpoints, csys);

    // Request PV image
    int width(3), z_min(0), z_max(5); // first 6 channels
    auto pv_request = Message::PvRequest(file_id, region_id, width, z_min, z_max);
    auto progress_callback = [&](float progress) {};
    CARTA::PvResponse pv_response;
    carta::GeneratedImage pv_image;
    region_handler.CalculatePvImage(pv_request, frame, progress_callback, pv_response, pv_image);

    EXPECT_EQ(pv_response.success(), true);
    EXPECT_EQ(pv_response.cancel(), false);
    EXPECT_NE(pv_image.image.get(), nullptr);

    // Check shape
    auto pv_image_shape = pv_image.image->shape();
    EXPECT_EQ(pv_image_shape.size(), 2);
    EXPECT_EQ(pv_image_shape(1), 6); // spectral axis is 6 channels
}

TEST_F(PvGeneratorTest, PvImageReversedAxes) {
    auto image_path = TestRoot() / "data/images/fits/noise_3d.fits"; // 10x10x10 image
    std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(image_path));
    std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));
    auto csys = frame->CoordinateSystem();

    // Set line region [0, 0] to [9, 9]
    carta::RegionHandler region_handler;
    int file_id(0), region_id(-1);
    std::vector<float> endpoints = {0.0, 0.0, 9.0, 9.0};
    SetPvCut(region_handler, file_id, region_id, endpoints, csys);

    // Request PV image
    int width(3), z_min(0), z_max(9); // all channels
    bool reverse(false);
    auto pv_request = Message::PvRequest(file_id, region_id, width, z_min, z_max, reverse);
    auto progress_callback = [&](float progress) {};
    CARTA::PvResponse pv_response;
    carta::GeneratedImage pv_image;
    region_handler.CalculatePvImage(pv_request, frame, progress_callback, pv_response, pv_image);
    EXPECT_EQ(pv_response.success(), true);
    EXPECT_NE(pv_image.image.get(), nullptr);
    auto pv_image_shape = pv_image.image->shape();

    // Request reverse PV image with same cut
    reverse = true;
    pv_request = Message::PvRequest(file_id, region_id, width, z_min, z_max, reverse);
    CARTA::PvResponse rev_pv_response;
    carta::GeneratedImage rev_pv_image;
    region_handler.CalculatePvImage(pv_request, frame, progress_callback, rev_pv_response, rev_pv_image);
    EXPECT_EQ(rev_pv_response.success(), true);
    EXPECT_NE(rev_pv_image.image.get(), nullptr);
    auto rev_pv_image_shape = rev_pv_image.image->shape();

    // Check reversed shape
    EXPECT_EQ(rev_pv_image_shape.size(), pv_image_shape.size());
    EXPECT_EQ(rev_pv_image_shape(0), pv_image_shape(1));
    EXPECT_EQ(rev_pv_image_shape(1), pv_image_shape(0));
}

TEST_F(PvGeneratorTest, PvImageKeep) {
    auto image_path = TestRoot() / "data/images/fits/noise_3d.fits"; // 10x10x10 image
    std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(image_path));
    std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));
    auto csys = frame->CoordinateSystem();

    // Set line region [0, 0] to [9, 9]
    carta::RegionHandler region_handler;
    int file_id(0), region_id(-1);
    std::vector<float> endpoints = {0.0, 0.0, 9.0, 9.0};
    SetPvCut(region_handler, file_id, region_id, endpoints, csys);

    // Request PV image
    int width(3), z_min(0), z_max(9); // all channels
    bool reverse(false), keep(false);
    auto pv_request = Message::PvRequest(file_id, region_id, width, z_min, z_max, reverse, keep);
    auto progress_callback = [&](float progress) {};
    CARTA::PvResponse pv_response;
    carta::GeneratedImage pv_image;
    region_handler.CalculatePvImage(pv_request, frame, progress_callback, pv_response, pv_image);
    // Check PV image file_id and name
    int index(0);
    EXPECT_EQ(pv_response.success(), true);
    EXPECT_EQ(pv_image.file_id, PV_ID_MULTIPLIER - index);
    EXPECT_TRUE(pv_image.name.find("pv.fits") != std::string::npos);

    // Request PV image, keeping the first
    keep = true;
    pv_request = Message::PvRequest(file_id, region_id, width, z_min, z_max, reverse, keep);
    CARTA::PvResponse pv_response2;
    carta::GeneratedImage pv_image2;
    region_handler.CalculatePvImage(pv_request, frame, progress_callback, pv_response2, pv_image2);
    // Check PV image file_id and name
    index++;
    EXPECT_EQ(pv_response2.success(), true);
    EXPECT_EQ(pv_image2.file_id, PV_ID_MULTIPLIER - index);
    EXPECT_TRUE(pv_image2.name.find("pv1.fits") != std::string::npos);

    // Request PV image, replace all and reset index
    keep = false;
    pv_request = Message::PvRequest(file_id, region_id, width, z_min, z_max, reverse, keep);
    CARTA::PvResponse pv_response3;
    carta::GeneratedImage pv_image3;
    region_handler.CalculatePvImage(pv_request, frame, progress_callback, pv_response3, pv_image3);
    // Check PV image file_id and name
    index = 0;
    EXPECT_EQ(pv_response3.success(), true);
    EXPECT_EQ(pv_image3.file_id, PV_ID_MULTIPLIER - index);
    EXPECT_TRUE(pv_image3.name.find("pv.fits") != std::string::npos);
}
