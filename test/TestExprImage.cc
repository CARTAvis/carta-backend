/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "CommonTestUtilities.h"
#include "ImageData/CartaHdf5Image.h"
#include "ImageData/FileLoader.h"
#include "Logger/Logger.h"

class ImageExprTest : public ::testing::Test {
public:
    void GenerateImageExprTimesTwo(const std::string& file_name, const std::string& hdu, CARTA::FileType file_type, bool invalid = false) {
        std::string file_path;
        if (file_type == CARTA::FileType::FITS) {
            file_path = FileFinder::FitsImagePath(file_name);
        } else if (file_type == CARTA::FileType::HDF5) {
            file_path = FileFinder::Hdf5ImagePath(file_name);
        }

        // Image on disk
        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(file_path));
        loader->OpenFile(hdu);
        casacore::IPosition image_shape(loader->GetShape());

        std::shared_ptr<DataReader> reader = nullptr;
        if (file_type == CARTA::FileType::HDF5) {
            reader.reset(new Hdf5DataReader(file_path));
        } else {
            reader.reset(new FitsDataReader(file_path));
        }

        auto image_xprofile = reader->ReadProfileX(0);
        auto image_yprofile = reader->ReadProfileY(0);

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

        std::shared_ptr<carta::FileLoader> expr_loader(carta::FileLoader::GetLoader(expr, directory));
        expr_loader->OpenFile(hdu);
        casacore::IPosition expr_shape(expr_loader->GetShape());

        // Slicer for x spatial profile at y=0
        casacore::IPosition start(expr_shape.size(), 0);
        casacore::IPosition end(start);
        end(0) = expr_shape(0) - 1;
        casacore::Slicer xslicer(start, end, casacore::Slicer::endIsLast);
        casacore::Array<float> expr_xprofile;
        expr_xprofile.resize(xslicer.length());
        expr_loader->GetSlice(expr_xprofile, xslicer);

        // Slicer for y spatial profile at x=0
        end = start;
        end(1) = expr_shape(1) - 1;
        casacore::Slicer yslicer(start, end, casacore::Slicer::endIsLast);
        casacore::Array<float> expr_yprofile;
        expr_yprofile.resize(yslicer.length());
        expr_loader->GetSlice(expr_yprofile, yslicer);

        // Tests
        ASSERT_EQ(expr_loader->GetImage()->imageType(), "ImageExpr");
        EXPECT_EQ(image_shape, expr_shape);
        // Compare image xprofile * 2 to expr xprofile
        for_each(image_xprofile.begin(), image_xprofile.end(), [](float& a) { a *= 2; });
        CmpVectors(image_xprofile, expr_xprofile.tovector());
        // Compare image yprofile * 2 to expr yprofile
        for_each(image_yprofile.begin(), image_yprofile.end(), [](float& a) { a *= 2; });
        CmpVectors(image_yprofile, expr_yprofile.tovector());
    }

    void SaveImageExpr(const std::string& file_name, const std::string& hdu, CARTA::FileType file_type) {
        std::string file_path;
        if (file_type == CARTA::FileType::FITS) {
            file_path = FileFinder::FitsImagePath(file_name);
        } else if (file_type == CARTA::FileType::HDF5) {
            file_path = FileFinder::Hdf5ImagePath(file_name);
        }

        // Use LEL expr to multiply image by 2
        fs::path fs_path(file_path);
        std::string expr = fs_path.filename().string() + " * 2";
        std::string directory = fs_path.parent_path().string();

        std::shared_ptr<carta::FileLoader> expr_loader(carta::FileLoader::GetLoader(expr, directory));
        expr_loader->OpenFile(hdu);
        casacore::IPosition expr_shape(expr_loader->GetShape());

        // Save LEL image, CASA format only allowed from loader (saves as LEL image)
        std::string save_path = (fs_path.parent_path() / "test_save_expr.im").string();
        std::string message;
        ASSERT_TRUE(expr_loader->SaveFile(CARTA::FileType::CASA, save_path, message));

        // Load saved image
        std::shared_ptr<carta::FileLoader> saved_expr_loader(carta::FileLoader::GetLoader(save_path));
        saved_expr_loader->OpenFile(hdu);
        ASSERT_TRUE(expr_loader->GetImage().get() != nullptr);
        ASSERT_EQ(expr_loader->GetImage()->imageType(), "ImageExpr");

        casacore::IPosition saved_expr_shape(saved_expr_loader->GetShape());
        ASSERT_EQ(expr_shape, saved_expr_shape);
    }
};

TEST_F(ImageExprTest, FitsImageExprTimesTwo) {
    GenerateImageExprTimesTwo("noise_10px_10px.fits", "0", CARTA::FileType::FITS);
}

TEST_F(ImageExprTest, Hdf5ImageExprTimesTwo) {
    GenerateImageExprTimesTwo("noise_10px_10px.hdf5", "", CARTA::FileType::HDF5);
}

TEST_F(ImageExprTest, FitsImageExprSave) {
    SaveImageExpr("noise_10px_10px.fits", "0", CARTA::FileType::FITS);
}

TEST_F(ImageExprTest, ImageExprFails) {
    // Forms invalid expression
    ASSERT_THROW(GenerateImageExprTimesTwo("noise_10px_10px.fits", "", CARTA::FileType::FITS, true), casacore::AipsError);
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
