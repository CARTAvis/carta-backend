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
#include "Main/ProgramSettings.h"

using namespace carta;

class ImageExprTest : public ::testing::Test {
public:
    std::string default_top_level_folder = ProgramSettings::GetInstance().top_level_folder;

    void TearDown() override {
        ProgramSettings::GetInstance().top_level_folder = default_top_level_folder;
    }

    void GenerateImageExprTimesTwo(const fs::path file_path, const std::string& hdu, bool invalid = false) {
        // Image on disk
        auto loader = carta::FileLoader::GetLoader(file_path);
        loader->OpenFile(hdu);
        casacore::IPosition image_shape(loader->GetShape());

        std::shared_ptr<DataReader> reader = nullptr;
        if (file_path.parent_path().filename() == "hdf5") {
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
            expr = "'" + fs_path.filename().string() + "' & 2";
        } else {
            // Use LEL expr to multiply image by 2
            expr = "'" + fs_path.filename().string() + "' * 2";
        }

        auto expr_loader = carta::FileLoader::GetLoader(expr, directory);
        expr_loader->OpenFile(hdu);
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

    void SaveImageExpr(const fs::path file_path, const std::string& hdu) {
        // Use LEL expr to multiply image by 2
        fs::path fs_path(file_path);
        std::string expr = "\"" + fs_path.filename().string() + "\" * 2";
        std::string directory = fs_path.parent_path().string();

        auto expr_loader = carta::FileLoader::GetLoader(expr, directory);
        expr_loader->OpenFile(hdu);
        casacore::IPosition expr_shape(expr_loader->GetShape());

        // Save LEL image, CASA format only allowed from loader (saves as LEL image)
        std::string save_path = (fs_path.parent_path() / "test_save_expr.im").string();
        std::string message;
        ASSERT_TRUE(expr_loader->SaveFile(CARTA::FileType::CASA, save_path, message));

        // Load saved image
        auto saved_expr_loader = carta::FileLoader::GetLoader(save_path);
        saved_expr_loader->OpenFile(hdu);
        ASSERT_TRUE(expr_loader->GetImage().get() != nullptr);
        ASSERT_EQ(expr_loader->GetImage()->imageType(), "ImageExpr");

        casacore::IPosition saved_expr_shape(saved_expr_loader->GetShape());
        ASSERT_EQ(expr_shape, saved_expr_shape);
    }
};

TEST_F(ImageExprTest, FitsImageExprTimesTwo) {
    GenerateImageExprTimesTwo(FitsImages() / "noise_10px_10px.fits", "0");
}

TEST_F(ImageExprTest, Hdf5ImageExprTimesTwo) {
    GenerateImageExprTimesTwo(Hdf5Images() / "noise_10px_10px.hdf5", "");
}

TEST_F(ImageExprTest, FitsImageExprSave) {
    SaveImageExpr(FitsImages() / "noise_10px_10px.fits", "0");
}

TEST_F(ImageExprTest, ImageExprFails) {
    // Forms invalid expression
    ASSERT_THROW(GenerateImageExprTimesTwo(FitsImages() / "noise_10px_10px.fits", "", true), casacore::AipsError);
}

TEST_F(ImageExprTest, ImageExprTwoDirs) {
    // Add images in different directories
    std::string expr = "\"noise_10px_10px.fits\" + \"../casa/noise_10px_10px.im\"";

    auto expr_loader = carta::FileLoader::GetLoader(expr, FitsImages());
    expr_loader->OpenFile("");
    casacore::IPosition expr_shape(expr_loader->GetShape());

    auto fits_path = FitsImages() / "noise_10px_10px.fits";
    auto fits_loader = carta::FileLoader::GetLoader(fits_path);
    fits_loader->OpenFile("");
    casacore::IPosition fits_shape(fits_loader->GetShape());
    ASSERT_EQ(fits_shape, expr_shape);
}

TEST_F(ImageExprTest, ImageExprOutsideTopLevelFolder) {
    auto& top_level_folder = ProgramSettings::GetInstance().top_level_folder;
    std::string two_dirs = "\"noise_10px_10px.fits\" + \"../casa/noise_10px_10px.im\"";
    auto saved_path = (FitsImages() / "test_two_dirs_expr.im").string();
    fs::remove_all(saved_path);
    std::string message;

    top_level_folder = ImagesPath().string();
    auto loader = carta::FileLoader::GetLoader(two_dirs, FitsImages());
    EXPECT_NO_THROW(loader->OpenFile(""));
    ASSERT_TRUE(loader->SaveFile(CARTA::FileType::CASA, saved_path, message));
    EXPECT_NO_THROW(carta::FileLoader::GetLoader(saved_path)->OpenFile(""));

    top_level_folder = FitsImages().string();
    EXPECT_THROW(carta::FileLoader::GetLoader(two_dirs, FitsImages())->OpenFile(""), casacore::AipsError);
    EXPECT_THROW(carta::FileLoader::GetLoader(saved_path)->OpenFile(""), casacore::AipsError);
    fs::remove_all(saved_path);

    // A name outside the folder is refused before casacore looks for it, so the error doesn't give away whether it exists
    try {
        carta::FileLoader::GetLoader("\"../casa/missing.im\" * 2", FitsImages())->OpenFile("");
        ADD_FAILURE() << "Expression with a name outside the top-level folder was opened";
    } catch (const casacore::AipsError& err) {
        EXPECT_NE(err.getMesg().find("top-level folder"), std::string::npos) << err.getMesg();
    }
}

TEST_F(ImageExprTest, RootFolderAllowsSavedExprInExpr) {
    // With the top-level folder /, nothing is refused, so an expression can still use a saved image expression
    ProgramSettings::GetInstance().top_level_folder = "/";
    auto saved_path = (FitsImages() / "test_saved_in_expr.im").string();
    fs::remove_all(saved_path);
    std::string message;
    auto loader = carta::FileLoader::GetLoader("\"" + (FitsImages() / "noise_10px_10px.fits").string() + "\" * 2", FitsImages());
    loader->OpenFile("");
    ASSERT_TRUE(loader->SaveFile(CARTA::FileType::CASA, saved_path, message));
    EXPECT_NO_THROW(carta::FileLoader::GetLoader("\"test_saved_in_expr.im\" * 1", FitsImages())->OpenFile(""));
    fs::remove_all(saved_path);
}

TEST_F(ImageExprTest, ImageExprInLinkedFolder) {
    // In a folder reached through a symlink, a .. in a name starts from where the symlink points
    auto link_dir = ImagesPath() / "test_link_dir";
    fs::remove_all(link_dir);
    fs::create_directory(link_dir);
    fs::create_directory_symlink(FitsImages(), link_dir / "link");

    ProgramSettings::GetInstance().top_level_folder = ImagesPath().string();
    auto loader = carta::FileLoader::GetLoader("\"noise_10px_10px.fits\" + \"../casa/noise_10px_10px.im\"", link_dir / "link");
    EXPECT_NO_THROW(loader->OpenFile(""));

    fs::remove_all(link_dir);
}
