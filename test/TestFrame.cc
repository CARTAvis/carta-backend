/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <casacore/coordinates/Coordinates/CoordinateUtil.h>
#include <casacore/images/Images/TempImage.h>
#include <gtest/gtest.h>
#include <type_traits>

#include "Frame/Frame.h"

#include "CommonTestUtilities.h"
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
    FRIEND_TEST(FrameTest, TestIsConnected);
    FRIEND_TEST(FrameTest, TestGetErrorMessage);
    FRIEND_TEST(FrameTest, TestWidth);
    FRIEND_TEST(FrameTest, TestHeight);
    FRIEND_TEST(FrameTest, TestDepth);
    FRIEND_TEST(FrameTest, TestNumStokes);
    FRIEND_TEST(FrameTest, TestCurrentZ);
    FRIEND_TEST(FrameTest, TestCurrentStokes);
    FRIEND_TEST(FrameTest, TestSpectralAxis);
    FRIEND_TEST(FrameTest, TestStokesAxis);
    FRIEND_TEST(FrameTest, TestImageShapeNotComputed);
};

// This macro simplifies adding tests for getters with no additional logic.
// The individual tests must be added as friends to TestFrame above.
#define TEST_SIMPLE_GETTER(gtr, atr, val)                                                                              \
    TEST(FrameTest, Test##gtr) {                                                                                       \
        TestFrame frame(0, nullptr, "0");                                                                              \
        ASSERT_TRUE((std::is_same_v<std::remove_cv_t<decltype(frame.atr)>, std::remove_cv_t<decltype(frame.gtr())>>)); \
        frame.atr = val;                                                                                               \
        ASSERT_EQ(frame.gtr(), val);                                                                                   \
    }

TEST(FrameTest, TestConstructorNotHDF5) {
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
    Factories::_mock_tile_caches.push(tile_cache);
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
    loader->MakeValid();
    EXPECT_CALL(*loader, LoadImageStats(_)).WillOnce(Throw(casacore::AipsError("These stats are bad.")));

    TestFrame frame(0, loader, "0");
    ASSERT_EQ(frame._valid, true);
    ASSERT_EQ(frame._open_image_error, "Problem loading statistics from file: These stats are bad.");
}

TEST(FrameTest, TestGetFileName) {
    auto loader = std::make_shared<NiceMock<MockFileLoader>>();
    EXPECT_CALL(*loader, GetFileName()).WillOnce(Return("somefile.fits"));

    TestFrame frame(0, loader, "0");
    ASSERT_EQ(frame.GetFileName(), "somefile.fits");
}

TEST_SIMPLE_GETTER(IsValid, _valid, true)
TEST_SIMPLE_GETTER(IsConnected, _connected, true)
TEST_SIMPLE_GETTER(GetErrorMessage, _open_image_error, "test")

TEST_SIMPLE_GETTER(Width, _width, 123)
TEST_SIMPLE_GETTER(Height, _height, 123)
TEST_SIMPLE_GETTER(Depth, _depth, 123)
TEST_SIMPLE_GETTER(NumStokes, _num_stokes, 123)

TEST_SIMPLE_GETTER(CurrentZ, _z_index, 123)
TEST_SIMPLE_GETTER(CurrentStokes, _stokes_index, 123)
TEST_SIMPLE_GETTER(SpectralAxis, _spectral_axis, 123)
TEST_SIMPLE_GETTER(StokesAxis, _stokes_axis, 123)

TEST(FrameTest, TestCoordinateSystem) {
    auto loader = std::make_shared<NiceMock<MockFileLoader>>();
    auto mock_csys =
        std::make_shared<casacore::CoordinateSystem>(casacore::CoordinateUtil::makeCoordinateSystem(casacore::IPosition{30, 20, 10, 4}));
    EXPECT_CALL(*loader, GetCoordinateSystem(_)).WillOnce(Return(mock_csys));

    TestFrame frame(0, loader, "0");

    // 0 means equality, and these should be the same object
    ASSERT_EQ(casacore::CoordinateUtil::compareCoordinates(*frame.CoordinateSystem(), *mock_csys), 0);
}

TEST(FrameTest, TestImageShapeNotComputed) {
    auto loader = std::make_shared<NiceMock<MockFileLoader>>();
    TestFrame frame(0, loader, "0");
    // Use cached shape on frame
    ASSERT_EQ(frame.ImageShape(), frame._image_shape);
}

TEST(FrameTest, TestImageShapeComputed) {
    auto loader = std::make_shared<NiceMock<MockFileLoader>>();
    // loader returns non-null image
    auto shape = casacore::IPosition{10, 10, 10, 1};
    auto temp_image = std::make_shared<casacore::TempImage<float>>();
    temp_image->resize(casacore::TiledShape(shape));
    EXPECT_CALL(*loader, GetStokesImage(_)).WillOnce(Return(temp_image));

    TestFrame frame(0, loader, "0");
    // A computed polarization
    StokesSource stokes_source(COMPUTE_STOKES_PTOTAL, AxisRange(0));
    // Should return shape of image returned from loader
    ASSERT_EQ(frame.ImageShape(stokes_source), shape);
}

TEST(FrameTest, TestImageShapeComputedFailure) {
    auto loader = std::make_shared<NiceMock<MockFileLoader>>();
    // loader returns null image
    EXPECT_CALL(*loader, GetStokesImage(_)).WillOnce(Return(nullptr));

    TestFrame frame(0, loader, "0");
    // A computed polarization
    StokesSource stokes_source(COMPUTE_STOKES_PTOTAL, AxisRange(0));
    // Should return default blank shape
    ASSERT_EQ(frame.ImageShape(stokes_source), casacore::IPosition());
}

TEST(FrameTest, TestGetBeams) {
    auto loader = std::make_shared<NiceMock<MockFileLoader>>();
    EXPECT_CALL(*loader, GetBeams(_, _)).WillOnce(DoAll(SetArgReferee<0>(std::vector<CARTA::Beam>(3)), Return(true)));
    EXPECT_CALL(*loader, CloseImageIfUpdated());

    TestFrame frame(0, loader, "0");
    std::vector<CARTA::Beam> beams;
    // Returns the value from the loader
    ASSERT_EQ(frame.GetBeams(beams), true);
    ASSERT_EQ(beams.size(), 3);
}

TEST(FrameTest, TestGetImageSlicer) {
    auto loader = std::make_shared<NiceMock<MockFileLoader>>();
    // Set up default dimensions and axes
    loader->MakeValid();
    TestFrame frame(0, loader, "0");

    ASSERT_EQ(frame.GetImageSlicer(AxisRange(ALL_Z), 0),
        StokesSlicer(StokesSource(0, AxisRange(ALL_Z)),
            casacore::Slicer(casacore::IPosition{0, 0, 0, 0}, casacore::IPosition{29, 19, 9, 0}, casacore::Slicer::endIsLast)));
}
