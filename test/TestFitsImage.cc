/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "ImageData/FileLoader.h"
#include "src/Frame/Frame.h"

#include "CommonTestUtilities.h"

using namespace carta;

// Allows testing of protected methods in Frame without polluting the original class
class TestFrame : public Frame {
public:
    TestFrame(uint32_t session_id, std::shared_ptr<carta::FileLoader> loader, const std::string& hdu, int default_z = DEFAULT_Z)
        : Frame(session_id, loader, hdu, default_z) {}
    FRIEND_TEST(FitsImageTest, ExampleFriendTest);
};

class FitsImageTest : public ::testing::Test {};

TEST_F(FitsImageTest, BasicLoadingTest) {
    auto path = FitsImages() / "10x10.fits";
    auto loader = carta::FileLoader::GetLoader(path);
    EXPECT_NE(loader.get(), nullptr);
    std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));
    EXPECT_NE(frame.get(), nullptr);
    EXPECT_TRUE(frame->IsValid());
}

TEST_F(FitsImageTest, ExampleFriendTest) {
    auto path = FitsImages() / "10x10.fits";
    // TestFrame used instead of Frame if access to protected values is required
    auto loader = carta::FileLoader::GetLoader(path);
    std::unique_ptr<TestFrame> frame(new TestFrame(0, loader, "0"));
    EXPECT_TRUE(frame->IsValid());
    EXPECT_TRUE(frame->_open_image_error.empty());
}

TEST_F(FitsImageTest, CorrectShape2dImage) {
    auto path = FitsImages() / "10x10.fits";
    auto loader = carta::FileLoader::GetLoader(path);
    std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));
    EXPECT_TRUE(frame->IsValid());

    auto shape = frame->ImageShape();
    EXPECT_EQ(shape.size(), 2);
    EXPECT_EQ(shape[0], 10);
    EXPECT_EQ(shape[1], 10);
    EXPECT_EQ(frame->Depth(), 1);
    EXPECT_EQ(frame->NumStokes(), 1);
}

TEST_F(FitsImageTest, CorrectShape3dImage) {
    auto path = FitsImages() / "10x10x10.fits";
    auto loader = carta::FileLoader::GetLoader(path);
    std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));
    EXPECT_TRUE(frame->IsValid());

    auto shape = frame->ImageShape();
    EXPECT_EQ(shape.size(), 3);
    EXPECT_EQ(shape[0], 10);
    EXPECT_EQ(shape[1], 10);
    EXPECT_EQ(shape[2], 10);
    EXPECT_EQ(frame->Depth(), 10);
    EXPECT_EQ(frame->NumStokes(), 1);
    EXPECT_EQ(frame->StokesAxis(), -1);
}

TEST_F(FitsImageTest, CorrectShapeDegenerate3dImages) {
    auto path = FitsImages() / "10x10x10x1.fits";
    auto loader = carta::FileLoader::GetLoader(path);
    std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));
    EXPECT_TRUE(frame->IsValid());

    auto shape = frame->ImageShape();
    EXPECT_EQ(shape.size(), 4);
    EXPECT_EQ(shape[0], 10);
    EXPECT_EQ(shape[1], 10);
    EXPECT_EQ(shape[2], 10);
    EXPECT_EQ(shape[3], 1);
    EXPECT_EQ(frame->Depth(), 10);
    EXPECT_EQ(frame->NumStokes(), 1);
    EXPECT_EQ(frame->StokesAxis(), 3);

    // CASA-generated images often have spectral and Stokes axes swapped
    path = FitsImages() / "10x10x1x10.fits";
    loader = carta::FileLoader::GetLoader(path);
    frame.reset(new Frame(0, loader, "0"));
    EXPECT_TRUE(frame->IsValid());

    shape = frame->ImageShape();
    EXPECT_EQ(shape.size(), 4);
    EXPECT_EQ(shape[0], 10);
    EXPECT_EQ(shape[1], 10);
    EXPECT_EQ(shape[2], 1);
    EXPECT_EQ(shape[3], 10);
    EXPECT_EQ(frame->Depth(), 10);
    EXPECT_EQ(frame->NumStokes(), 1);
    EXPECT_EQ(frame->StokesAxis(), 2);
}

TEST_F(FitsImageTest, CorrectShape4dImages) {
    auto path = FitsImages() / "10x10x5x2.fits";
    auto loader = carta::FileLoader::GetLoader(path);
    std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));
    EXPECT_TRUE(frame->IsValid());

    auto shape = frame->ImageShape();
    EXPECT_EQ(shape.size(), 4);
    EXPECT_EQ(shape[0], 10);
    EXPECT_EQ(shape[1], 10);
    EXPECT_EQ(shape[2], 5);
    EXPECT_EQ(shape[3], 2);
    EXPECT_EQ(frame->Depth(), 5);
    EXPECT_EQ(frame->NumStokes(), 2);
    EXPECT_EQ(frame->StokesAxis(), 3);

    // CASA-generated images often have spectral and Stokes axes swapped
    path = FitsImages() / "10x10x2x5.fits";
    loader = carta::FileLoader::GetLoader(path);
    frame.reset(new Frame(0, loader, "0"));
    EXPECT_TRUE(frame->IsValid());

    shape = frame->ImageShape();
    EXPECT_EQ(shape.size(), 4);
    EXPECT_EQ(shape[0], 10);
    EXPECT_EQ(shape[1], 10);
    EXPECT_EQ(shape[2], 2);
    EXPECT_EQ(shape[3], 5);
    EXPECT_EQ(frame->Depth(), 5);
    EXPECT_EQ(frame->NumStokes(), 2);
    EXPECT_EQ(frame->StokesAxis(), 2);
}

TEST_F(FitsImageTest, ScientificNotationParsing) {
    auto tmp_path = fs::temp_directory_path() / "carta_scientific_cdeIt_test.fits";

    fitsfile* fptr = nullptr;
    int status = 0;

    // Create a minimal FITS image with scientific-notation CDELT values.
    fits_create_file(&fptr, tmp_path.string().c_str(), &status);
    ASSERT_EQ(status, 0);

    long naxis = 2;
    long naxes[2] = {10, 10};
    fits_create_img(fptr, FLOAT_IMG, naxis, naxes, &status);
    ASSERT_EQ(status, 0);

    char ctype1[] = "RA---TAN";
    char ctype2[] = "DEC--TAN";
    char cunit1[] = "deg";
    char cunit2[] = "deg";
    double crpix1 = 1.0;
    double crpix2 = 1.0;
    double crval1 = 0.0;
    double crval2 = 0.0;
    double cdelt1 = 5e-05;
    double cdelt2 = 5e-05;

    fits_write_key(fptr, TSTRING, "CTYPE1", ctype1, nullptr, &status);
    fits_write_key(fptr, TSTRING, "CTYPE2", ctype2, nullptr, &status);
    fits_write_key(fptr, TSTRING, "CUNIT1", cunit1, nullptr, &status);
    fits_write_key(fptr, TSTRING, "CUNIT2", cunit2, nullptr, &status);
    fits_write_key(fptr, TDOUBLE, "CRPIX1", &crpix1, nullptr, &status);
    fits_write_key(fptr, TDOUBLE, "CRPIX2", &crpix2, nullptr, &status);
    fits_write_key(fptr, TDOUBLE, "CRVAL1", &crval1, nullptr, &status);
    fits_write_key(fptr, TDOUBLE, "CRVAL2", &crval2, nullptr, &status);
    fits_write_key(fptr, TDOUBLE, "CDELT1", &cdelt1, nullptr, &status);
    fits_write_key(fptr, TDOUBLE, "CDELT2", &cdelt2, nullptr, &status);

    fits_close_file(fptr, &status);
    ASSERT_EQ(status, 0);

    auto loader = carta::FileLoader::GetLoader(tmp_path);
    ASSERT_NE(loader.get(), nullptr);

    std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));
    EXPECT_TRUE(frame->IsValid());

    auto coord_sys = frame->CoordinateSystem();
    ASSERT_NE(coord_sys, nullptr);

    auto increment = coord_sys->directionCoordinate().increment();
    ASSERT_GE(increment.nelements(), 2);

    EXPECT_NEAR(casacore::Quantity(increment(0), "rad").getValue("deg"), 5e-05, 1e-12);
    EXPECT_NEAR(casacore::Quantity(increment(1), "rad").getValue("deg"), 5e-05, 1e-12);
    
    std::error_code ec;
    fs::remove(tmp_path, ec);
}
