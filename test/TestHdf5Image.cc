/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/
#include <unordered_set>

#include <gtest/gtest.h>

#include "ImageData/FileLoader.h"
#include "src/Frame/Frame.h"

#include "CommonTestUtilities.h"

using namespace carta;

class Hdf5ImageTest : public ::testing::Test, public ImageGenerator {};

TEST_F(Hdf5ImageTest, BasicLoadingTest) {
    auto path_string = GeneratedHdf5ImagePath("10 10");
    std::shared_ptr<carta::FileLoader> loader(carta::BaseFileLoader::GetLoader(path_string));
    EXPECT_NE(loader.get(), nullptr);
    std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));
    EXPECT_NE(frame.get(), nullptr);
}

TEST_F(Hdf5ImageTest, CorrectShape2dImage) {
    auto path_string = GeneratedHdf5ImagePath("10 10");
    std::shared_ptr<carta::FileLoader> loader(carta::BaseFileLoader::GetLoader(path_string));
    std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));

    auto shape = frame->ImageShape();
    EXPECT_EQ(shape.size(), 2);
    EXPECT_EQ(shape[0], 10);
    EXPECT_EQ(shape[1], 10);
    EXPECT_EQ(frame->Depth(), 1);
    EXPECT_EQ(frame->NumStokes(), 1);
}

TEST_F(Hdf5ImageTest, CorrectShape3dImage) {
    auto path_string = GeneratedHdf5ImagePath("10 10 10");
    std::shared_ptr<carta::FileLoader> loader(carta::BaseFileLoader::GetLoader(path_string));
    std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));

    auto shape = frame->ImageShape();
    EXPECT_EQ(shape.size(), 3);
    EXPECT_EQ(shape[0], 10);
    EXPECT_EQ(shape[1], 10);
    EXPECT_EQ(shape[2], 10);
    EXPECT_EQ(frame->Depth(), 10);
    EXPECT_EQ(frame->NumStokes(), 1);
    EXPECT_EQ(frame->StokesAxis(), -1);
}

TEST_F(Hdf5ImageTest, CorrectShapeDegenerate3dImages) {
    auto path_string = GeneratedHdf5ImagePath("10 10 10 1");
    std::shared_ptr<carta::FileLoader> loader(carta::BaseFileLoader::GetLoader(path_string));
    std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));

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
    path_string = GeneratedHdf5ImagePath("10 10 1 10");
    loader.reset(carta::BaseFileLoader::GetLoader(path_string));
    frame.reset(new Frame(0, loader, "0"));

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

TEST_F(Hdf5ImageTest, CorrectShape4dImages) {
    auto path_string = GeneratedHdf5ImagePath("10 10 5 2");
    std::shared_ptr<carta::FileLoader> loader(carta::BaseFileLoader::GetLoader(path_string));
    std::unique_ptr<Frame> frame(new Frame(0, loader, "0"));

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
    path_string = GeneratedHdf5ImagePath("10 10 2 5");
    loader.reset(carta::BaseFileLoader::GetLoader(path_string));
    frame.reset(new Frame(0, loader, "0"));

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
