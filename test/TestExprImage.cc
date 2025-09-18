/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "CommonTestUtilities.h"
#include "ImageData/CartaHdf5Image.h"
#include "ImageData/FileLoader.h"
#include "Logger/Logger.h"

using namespace carta;

struct ImageExprInfo {
    std::string file_path; // full path to the image file
    std::string directory; // directory containing the image file
    std::string expr;      // LEL expression for the image
};

class ImageExprTest : public ::testing::Test {
public:
    // Helper function to construct file paths and LEL expressions for an image.
    //
    // Behavior:
    //   * Determines the full file path based on the file type (FITS or HDF5).
    //   * Extracts the directory containing the file.
    //   * Constructs a LEL expression for the file. By default, the expression
    //     multiplies the image by 2. If 'invalid' is true, generates a deliberately
    //     invalid expression for testing error handling.
    //
    // This function centralizes path and expression creation to reduce code duplication
    // in tests that generate or save expression-based images.
    ImageExprInfo PrepareImageExpr(const std::string& file_name, CARTA::FileType file_type, bool invalid = false) {
        std::string file_path;
        if (file_type == CARTA::FileType::FITS) {
            file_path = FileFinder::FitsImagePath(file_name);
        } else if (file_type == CARTA::FileType::HDF5) {
            file_path = FileFinder::Hdf5ImagePath(file_name);
        }

        fs::path fs_path(file_path);
        std::string directory = fs_path.parent_path().string();

        std::string expr;
        if (invalid) {
            // Use LEL expr with invalid syntax
            expr = fs_path.filename().string() + " & 2";
        } else {
            // Use LEL expr to multiply image by 2
            expr = fs_path.filename().string() + " * 2";
        }

        return {file_path, directory, expr};
    }
};

struct ImageExprParam {
    std::string file_name; // name of the file to be tested
    std::string hdu;
    CARTA::FileType file_type; // the type of the input file (FITS or HDF5)
    bool invalid = false;
};

class ImageExprParamTest : public ImageExprTest, public ::testing::WithParamInterface<ImageExprParam> {};

// Parameterized test to verify correct evaluation of LEL image expressions.
//
// Behavior:
//   * Loads a test image from disk (FITS or HDF5) and reads its X/Y spatial profiles.
//   * Constructs an image expression that multiplies the original image by 2.
//   * If 'invalid' is set, ensures that opening the expression throws an error.
//   * Otherwise, opens the expression image, extracts X and Y profiles via slicing,
//     and compares them to the original profiles scaled by 2.
//   * Confirms that the expression image reports type "ImageExpr" and has the
//     same shape as the original image.
//
// This test ensures that valid LEL expressions produce the expected results
// across multiple file types, while invalid expressions fail gracefully.
TEST_P(ImageExprParamTest, TimesTwo) {
    const auto& param = GetParam();

    ImageExprInfo info = PrepareImageExpr(param.file_name, param.file_type, param.invalid);

    // Image on disk
    std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(info.file_path));
    loader->OpenFile(param.hdu);
    casacore::IPosition image_shape(loader->GetShape());

    std::shared_ptr<DataReader> reader = nullptr;
    if (param.file_type == CARTA::FileType::HDF5) {
        reader.reset(new Hdf5DataReader(info.file_path));
    } else {
        reader.reset(new FitsDataReader(info.file_path));
    }

    auto image_xprofile = reader->ReadProfileX(0);
    auto image_yprofile = reader->ReadProfileY(0);

    std::shared_ptr<carta::FileLoader> expr_loader(carta::FileLoader::GetLoader(info.expr, info.directory));

    if (param.invalid) {
        ASSERT_THROW(expr_loader->OpenFile(param.hdu), casacore::AipsError);
        return;
    }

    expr_loader->OpenFile(param.hdu);
    casacore::IPosition expr_shape(expr_loader->GetShape());

    // Slicer for x spatial profile at y=0
    casacore::IPosition start(expr_shape.size(), 0);
    casacore::IPosition end(start);
    end(0) = expr_shape(0) - 1;
    casacore::Slicer xslicer(start, end, casacore::Slicer::endIsLast);
    casacore::Array<float> expr_xprofile;
    expr_xprofile.resize(xslicer.length());
    expr_loader->GetSlice(expr_xprofile, carta::StokesSlicer(StokesSource(), xslicer));

    // Slicer for y spatial profile at x=0
    end = start;
    end(1) = expr_shape(1) - 1;
    casacore::Slicer yslicer(start, end, casacore::Slicer::endIsLast);
    casacore::Array<float> expr_yprofile;
    expr_yprofile.resize(yslicer.length());
    expr_loader->GetSlice(expr_yprofile, carta::StokesSlicer(StokesSource(), yslicer));

    // Tests
    ASSERT_EQ(expr_loader->GetImage()->imageType(), "ImageExpr");
    EXPECT_EQ(image_shape, expr_shape);
    // Compare image xprofile * 2 to expr xprofile
    for_each(image_xprofile.begin(), image_xprofile.end(), [](float& a) { a *= 2; });
    CmpVectors<float>(image_xprofile, expr_xprofile.tovector());
    // Compare image yprofile * 2 to expr yprofile
    for_each(image_yprofile.begin(), image_yprofile.end(), [](float& a) { a *= 2; });
    CmpVectors<float>(image_yprofile, expr_yprofile.tovector());
}

INSTANTIATE_TEST_SUITE_P(ImageExprTests, ImageExprParamTest,
    ::testing::Values(ImageExprParam{"noise_10px_10px.fits", "0", CARTA::FileType::FITS}, // FitsImageExprTimesTwo
        ImageExprParam{"noise_10px_10px.hdf5", "", CARTA::FileType::HDF5},                // Hdf5ImageExprTimesTwo
        ImageExprParam{"noise_10px_10px.fits", "", CARTA::FileType::FITS, true}           // ImageExprFails
        ));

TEST_F(ImageExprTest, FitsImageExprSave) {
    auto info = PrepareImageExpr("noise_10px_10px.fits", CARTA::FileType::FITS);

    std::shared_ptr<carta::FileLoader> expr_loader(carta::FileLoader::GetLoader(info.expr, info.directory));
    expr_loader->OpenFile("0");
    casacore::IPosition expr_shape(expr_loader->GetShape());

    std::string save_path = (fs::path(info.directory) / "test_save_expr.im").string();
    std::string message;
    ASSERT_TRUE(expr_loader->SaveFile(CARTA::FileType::CASA, save_path, message));

    std::shared_ptr<carta::FileLoader> saved_expr_loader(carta::FileLoader::GetLoader(save_path));
    saved_expr_loader->OpenFile("0");
    ASSERT_EQ(expr_shape, casacore::IPosition(saved_expr_loader->GetShape()));
}

TEST_F(ImageExprTest, ImageExprTwoDirs) {
    // Add images in different directories
    auto image_path = TestRoot() / "data/images/fits";
    std::string directory = image_path.string();
    std::string expr = "noise_10px_10px.fits + '../casa/noise_10px_10px.im'";

    std::shared_ptr<carta::FileLoader> expr_loader(carta::FileLoader::GetLoader(expr, directory));
    expr_loader->OpenFile("");
    casacore::IPosition expr_shape(expr_loader->GetShape());

    auto fits_path = FileFinder::FitsImagePath("noise_10px_10px.fits");
    std::shared_ptr<carta::FileLoader> fits_loader(carta::FileLoader::GetLoader(fits_path));
    fits_loader->OpenFile("");
    casacore::IPosition fits_shape(fits_loader->GetShape());
    ASSERT_EQ(fits_shape, expr_shape);
}
