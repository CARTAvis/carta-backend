/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "CommonTestUtilities.h"
#include "ImageData/FileLoader.h"

class ImageExprTest : public ::testing::Test {
public:
    void GenerateImageExprTimesTwo(const std::string& file_name, const std::string& hdu, CARTA::FileType file_type) {
        std::string file_path;
        if (file_type == CARTA::FileType::FITS) {
            file_path = FileFinder::FitsImagePath(file_name);
        } else if (file_type == CARTA::FileType::HDF5) {
            file_path = FileFinder::Hdf5ImagePath(file_name);
        }

        // Image on disk
        std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(file_path));
        loader->OpenFile(hdu);
        casacore::IPosition image_shape;
        loader->GetShape(image_shape);

        std::shared_ptr<DataReader> reader = nullptr;
        if (file_type == CARTA::FileType::HDF5) {
            reader.reset(new Hdf5DataReader(file_path));
        } else {
            reader.reset(new FitsDataReader(file_path));
        }

        auto image_xprofile = reader->ReadProfileX(0);
        auto image_yprofile = reader->ReadProfileY(0);

        // Use LEL expr to multiply image by 2
        fs::path fs_path(file_path);
        std::string expr = fs_path.filename().string() + " * 2";
        std::string directory = fs_path.parent_path().string();

        std::shared_ptr<carta::FileLoader> expr_loader(carta::FileLoader::GetLoader(expr, directory));
        expr_loader->OpenFile(hdu);
        casacore::IPosition expr_shape;
        expr_loader->GetShape(expr_shape);

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
};

TEST_F(ImageExprTest, FitsImageExprTimesTwo) {
    GenerateImageExprTimesTwo("noise_10px_10px.fits", "0", CARTA::FileType::FITS);
}

/*
TEST_F(ImageExprTest, Hdf5ImageExprTimesTwo) {
    GenerateImageExprTimesTwo("noise_10px_10px.hdf5", "", CARTA::FileType::HDF5);
}
*/
