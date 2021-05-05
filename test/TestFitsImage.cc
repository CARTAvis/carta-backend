/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/
#include <gtest/gtest.h>

#include "Frame.h"
#include "ImageData/FileLoader.h"
#include "Util.h"

#ifdef _BOOST_FILESYSTEM_
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

using namespace std;
using namespace carta;

// Allows testing of protected methods in Frame without polluting the original class
class TestFrame : public Frame {
public:
    TestFrame(uint32_t session_id, carta::FileLoader* loader, const std::string& hdu, int default_z = DEFAULT_Z)
        : Frame(session_id, loader, hdu, default_z) {}
    FRIEND_TEST(FitsImageTest, ExampleFriendTest);
};

class FitsImageTest : public ::testing::Test {
public:
    static string ImagePath(const string& filename) {
        string path_string;
        fs::path path;
        if (FindExecutablePath(path_string)) {
            path = fs::path(path_string).parent_path();
        } else {
            path = fs::current_path();
        }
        return (path / "data/images/fits" / filename).string();
    }
};

TEST_F(FitsImageTest, BasicLoadingTest) {
    auto path_string = ImagePath("noise_10px_10px.fits");
    std::unique_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(path_string));
    EXPECT_NE(loader.get(), nullptr);
    std::unique_ptr<Frame> frame(new Frame(0, loader.release(), "0"));
    EXPECT_NE(frame.get(), nullptr);
    EXPECT_TRUE(frame->IsValid());
}

TEST_F(FitsImageTest, ExampleFriendTest) {
    auto path_string = ImagePath("noise_10px_10px.fits");
    // TestFrame used instead of Frame if access to protected values is required
    std::unique_ptr<TestFrame> frame(new TestFrame(0, carta::FileLoader::GetLoader(path_string), "0"));
    EXPECT_TRUE(frame->IsValid());
    EXPECT_TRUE(frame->_open_image_error.empty());
}

TEST_F(FitsImageTest, CorrectShape2dImage) {
    auto path_string = ImagePath("noise_10px_10px.fits");
    std::unique_ptr<Frame> frame(new Frame(0, carta::FileLoader::GetLoader(path_string), "0"));
    EXPECT_TRUE(frame->IsValid());

    auto shape = frame->ImageShape();
    EXPECT_EQ(shape.size(), 2);
    EXPECT_EQ(shape[0], 10);
    EXPECT_EQ(shape[1], 10);
    EXPECT_EQ(frame->Depth(), 1);
    EXPECT_EQ(frame->NumStokes(), 1);
}

TEST_F(FitsImageTest, CorrectShape3dImage) {
    auto path_string = ImagePath("noise_3d.fits");
    std::unique_ptr<Frame> frame(new Frame(0, carta::FileLoader::GetLoader(path_string), "0"));
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
    auto path_string = ImagePath("noise_3d_degen.fits");
    std::unique_ptr<Frame> frame(new Frame(0, carta::FileLoader::GetLoader(path_string), "0"));
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
    path_string = ImagePath("noise_3d_degen_casa.fits");
    frame.reset(new Frame(0, carta::FileLoader::GetLoader(path_string), "0"));
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
    auto path_string = ImagePath("noise_4d.fits");
    std::unique_ptr<Frame> frame(new Frame(0, carta::FileLoader::GetLoader(path_string), "0"));
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
    path_string = ImagePath("noise_4d_casa.fits");
    frame.reset(new Frame(0, carta::FileLoader::GetLoader(path_string), "0"));
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