/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "Frame/Frame.h"

#include "Factories.h"
#include "MockFileLoader.h"
#include "MockTileCache.h"

using namespace carta;
using ::testing::_;
using ::testing::DoAll;
using ::testing::Exactly;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgReferee;
using ::testing::Throw;

// Allows testing of protected methods in Frame without polluting the original class
class TestFrame : public Frame {
public:
    TestFrame(uint32_t session_id, std::shared_ptr<carta::FileLoader> loader, const std::string& hdu, int default_z = DEFAULT_Z)
        : Frame(session_id, loader, hdu, default_z) {}
    FRIEND_TEST(FrameTest, TestConstructorNotHDF5);
    FRIEND_TEST(FrameTest, TestConstructorHDF5);
    FRIEND_TEST(FrameTest, TestNullLoader);
    FRIEND_TEST(FrameTest, TestBadLoader);
    FRIEND_TEST(FrameTest, TestNoLoaderShape);
    FRIEND_TEST(FrameTest, TestNoLoaderData);
    FRIEND_TEST(FrameTest, TestBadLoaderStats);
    FRIEND_TEST(FrameTest, TestIsValid);
    FRIEND_TEST(FrameTest, TestGetErrorMessage);
    FRIEND_TEST(FrameTest, TestGetFileName);
    FRIEND_TEST(FrameTest, TestGetFileNameNoLoader);
    //     FRIEND_TEST(FrameTest, );
};

TEST(FrameTest, TestConstructorNotHDF5) {
    // TODO extract defaults to a helper function
    // TODO maybe a constructor
    auto loader = std::make_shared<NiceMock<MockFileLoader>>();
    EXPECT_CALL(*loader, OpenFile("0"));
    EXPECT_CALL(*loader, FindCoordinateAxes(_, _, _, _, _))
        .WillOnce(DoAll(SetArgReferee<0>(casacore::IPosition{30, 20, 10, 4}), SetArgReferee<1>(2), SetArgReferee<2>(2), SetArgReferee<3>(3),
            Return(true)));
    EXPECT_CALL(*loader, GetRenderAxes()).WillOnce(Return(std::vector<int>{0, 1}));

    // Does not use tile cache; will load image cache
    EXPECT_CALL(*loader, UseTileCache()).Times(Exactly(2));
    EXPECT_CALL(*loader, GetSlice(_, _)).WillOnce(Return(true));
    EXPECT_CALL(*loader, CloseImageIfUpdated()).Times(Exactly(2));

    EXPECT_CALL(*loader, LoadImageStats(_));

    TestFrame frame(0, loader, "0");

    ASSERT_EQ(frame._valid, true);

    ASSERT_EQ(frame._x_axis, 0);
    ASSERT_EQ(frame._y_axis, 1);
    ASSERT_EQ(frame._width, 30);
    ASSERT_EQ(frame._height, 20);
    ASSERT_EQ(frame._depth, 10);
    ASSERT_EQ(frame._num_stokes, 4);

    ASSERT_EQ(frame._cube_histogram_configs.size(), 0);
    ASSERT_EQ(frame._image_histogram_configs.size(), 1);
    ASSERT_EQ(frame._image_histogram_configs[0], HistogramConfig("z", CURRENT_Z, AUTO_BIN_SIZE));
}

TEST(FrameTest, TestConstructorHDF5) {
    auto loader = std::make_shared<NiceMock<MockFileLoader>>();
    // TODO extract defaults to a helper function
    // TODO maybe a constructor
    EXPECT_CALL(*loader, OpenFile("0"));
    EXPECT_CALL(*loader, FindCoordinateAxes(_, _, _, _, _))
        .WillOnce(DoAll(SetArgReferee<0>(casacore::IPosition{1000, 750, 10, 4}), SetArgReferee<1>(2), SetArgReferee<2>(2),
            SetArgReferee<3>(3), Return(true)));
    EXPECT_CALL(*loader, GetRenderAxes()).WillOnce(Return(std::vector<int>{0, 1}));
    // Uses tile cache; will not load image cache
    EXPECT_CALL(*loader, UseTileCache()).Times(Exactly(2)).WillRepeatedly(Return(true));
    EXPECT_CALL(*loader, HasMip(2)).WillOnce(Return(true));
    EXPECT_CALL(*loader, CloseImageIfUpdated());
    EXPECT_CALL(*loader, LoadImageStats(_));

    MockTileCache* tile_cache = new MockTileCache();
    Factories::_mock_tile_cache = tile_cache;
    EXPECT_CALL(*tile_cache, Reset(0, 0, 14));

    TestFrame frame(0, loader, "0");

    ASSERT_EQ(frame._valid, true);

    ASSERT_EQ(frame._x_axis, 0);
    ASSERT_EQ(frame._y_axis, 1);
    ASSERT_EQ(frame._width, 1000);
    ASSERT_EQ(frame._height, 750);
    ASSERT_EQ(frame._depth, 10);
    ASSERT_EQ(frame._num_stokes, 4);

    ASSERT_EQ(frame._cube_histogram_configs.size(), 0);
    ASSERT_EQ(frame._image_histogram_configs.size(), 1);
    ASSERT_EQ(frame._image_histogram_configs[0], HistogramConfig("z", CURRENT_Z, AUTO_BIN_SIZE));
}

TEST(FrameTest, TestNullLoader) {
    TestFrame frame(0, nullptr, "0");
    ASSERT_EQ(frame._valid, false);
    ASSERT_EQ(frame._open_image_error, "Problem loading image: image type not supported.");
}

TEST(FrameTest, TestBadLoader) {
    auto loader = std::make_shared<NiceMock<MockFileLoader>>();
    EXPECT_CALL(*loader, OpenFile("0")).WillOnce(Throw(casacore::AipsError("This loader is bad.")));

    TestFrame frame(0, loader, "0");
    ASSERT_EQ(frame._valid, false);
    ASSERT_EQ(frame._open_image_error, "This loader is bad.");
}

TEST(FrameTest, TestNoLoaderShape) {
    auto loader = std::make_shared<NiceMock<MockFileLoader>>();
    EXPECT_CALL(*loader, FindCoordinateAxes(_, _, _, _, _)).WillOnce(DoAll(SetArgReferee<4>("No shape!"), Return(false)));

    TestFrame frame(0, loader, "0");
    ASSERT_EQ(frame._valid, false);
    ASSERT_EQ(frame._open_image_error, "Cannot determine file shape. No shape!");
}

TEST(FrameTest, TestNoLoaderData) {
    auto loader = std::make_shared<NiceMock<MockFileLoader>>();
    EXPECT_CALL(*loader, FindCoordinateAxes(_, _, _, _, _))
        .WillOnce(DoAll(SetArgReferee<0>(casacore::IPosition{30, 20, 10, 4}), SetArgReferee<1>(2), SetArgReferee<2>(2), SetArgReferee<3>(3),
            Return(true)));
    EXPECT_CALL(*loader, GetRenderAxes()).WillOnce(Return(std::vector<int>{0, 1}));
    EXPECT_CALL(*loader, GetSlice(_, _)).WillOnce(Return(false));

    TestFrame frame(0, loader, "0");
    ASSERT_EQ(frame._valid, false);
    ASSERT_EQ(frame._open_image_error, "Cannot load image data. Check log.");
}

TEST(FrameTest, TestBadLoaderStats) {
    auto loader = std::make_shared<NiceMock<MockFileLoader>>();
    EXPECT_CALL(*loader, FindCoordinateAxes(_, _, _, _, _))
        .WillOnce(DoAll(SetArgReferee<0>(casacore::IPosition{30, 20, 10, 4}), SetArgReferee<1>(2), SetArgReferee<2>(2), SetArgReferee<3>(3),
            Return(true)));
    EXPECT_CALL(*loader, GetRenderAxes()).WillOnce(Return(std::vector<int>{0, 1}));
    EXPECT_CALL(*loader, GetSlice(_, _)).WillOnce(Return(true));
    EXPECT_CALL(*loader, LoadImageStats(_)).WillOnce(Throw(casacore::AipsError("These stats are bad.")));

    TestFrame frame(0, loader, "0");
    ASSERT_EQ(frame._valid, true);
    ASSERT_EQ(frame._open_image_error, "Problem loading statistics from file: These stats are bad.");
}

TEST(FrameTest, TestIsValid) {
    TestFrame frame(0, nullptr, "0");
    frame._valid = true;
    ASSERT_EQ(frame.IsValid(), true);
    frame._valid = false;
    ASSERT_EQ(frame.IsValid(), false);
}

TEST(FrameTest, TestGetErrorMessage) {
    TestFrame frame(0, nullptr, "0");
    frame._open_image_error = "Custom error";
    ASSERT_EQ(frame.GetErrorMessage(), "Custom error");
}

TEST(FrameTest, TestGetFileName) {
    auto loader = std::make_shared<NiceMock<MockFileLoader>>();
    EXPECT_CALL(*loader, GetFileName()).WillOnce(Return("somefile.fits"));

    TestFrame frame(0, loader, "0");
    ASSERT_EQ(frame.GetFileName(), "somefile.fits");
}

TEST(FrameTest, TestGetFileNameNoLoader) {
    TestFrame frame(0, nullptr, "0");
    ASSERT_EQ(frame.GetFileName(), "");
}

// TEST(FrameTest, Test) {
// }
