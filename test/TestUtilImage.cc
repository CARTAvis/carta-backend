/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include "Util/Image.h"

class ImageUtilTest : public ::testing::Test {

};

TEST_F(ImageUtilTest, DefaultAxisRangeConstructor) {
    AxisRange range;
    EXPECT_EQ(range.from, 0);
    EXPECT_EQ(range.to, ALL_Z);
}

TEST_F(ImageUtilTest, SingleValueAxisRangeConstructor) {
    AxisRange range(5);
    EXPECT_EQ(range.from, 5);
    EXPECT_EQ(range.to, 5);
}

TEST_F(ImageUtilTest, AxisRangeConstructor) {
    AxisRange range(2, 8);
    EXPECT_EQ(range.from, 2);
    EXPECT_EQ(range.to, 8);
}

TEST_F(ImageUtilTest, RangeEqualityOperator) {
    AxisRange range1(3, 7);
    AxisRange range2(3, 7);
    AxisRange range3(4, 7);
    AxisRange range4(3, 8);
    
    EXPECT_TRUE(range1 == range2);
    EXPECT_FALSE(range1 == range3);
    EXPECT_FALSE(range1 == range4);
}

TEST_F(ImageUtilTest, RangeInequalityOperator) {
    AxisRange range1(3, 7);
    AxisRange range2(4, 7);
    
    EXPECT_TRUE(range1 != range2);
    EXPECT_FALSE(range1 != range1);
}

TEST_F(ImageUtilTest, IsInAxisRange) {
    AxisRange range(10, 20);
    EXPECT_TRUE(range.is_in_range(15));
    EXPECT_TRUE(range.is_in_range(10));
    EXPECT_TRUE(range.is_in_range(20));
    EXPECT_FALSE(range.is_in_range(9));
    EXPECT_FALSE(range.is_in_range(21));
}

TEST_F(ImageUtilTest, NegativeAxisRange) {
    AxisRange range(-10, -5);
    EXPECT_TRUE(range.is_in_range(-7));
    EXPECT_TRUE(range.is_in_range(-10));
    EXPECT_TRUE(range.is_in_range(-5));
    EXPECT_FALSE(range.is_in_range(-11));
    EXPECT_FALSE(range.is_in_range(-4));
}

TEST_F(ImageUtilTest, ReversedAxisRange) {
    AxisRange range(8, 3);  // This might indicate an invalid range
    EXPECT_FALSE(range.is_in_range(5));  // Should be out-of-range in a valid case
}

TEST_F(ImageUtilTest, LargeAxisRangeValues) {
    AxisRange range(1'000'000, 2'000'000);
    EXPECT_TRUE(range.is_in_range(1'500'000));
    EXPECT_FALSE(range.is_in_range(999'999));
}

TEST_F(ImageUtilTest, DefaultPointXYConstructor) {
    PointXy p;
    EXPECT_FLOAT_EQ(p.x, -1.0);
    EXPECT_FLOAT_EQ(p.y, -1.0);
}

TEST_F(ImageUtilTest, ParameterizedPointXYConstructor) {
    PointXy p(3.5, 7.2);
    EXPECT_FLOAT_EQ(p.x, 3.5);
    EXPECT_FLOAT_EQ(p.y, 7.2);
}

TEST_F(ImageUtilTest, PointXYAssignmentOperator) {
    PointXy p1(2.2, 4.4);
    PointXy p2;
    p2 = p1;
    EXPECT_FLOAT_EQ(p2.x, 2.2);
    EXPECT_FLOAT_EQ(p2.y, 4.4);
}

TEST_F(ImageUtilTest, PointXYEqualityOperator) {
    PointXy p1(1.1, 2.2);
    PointXy p2(1.1, 2.2);
    PointXy p3(3.3, 4.4);

    EXPECT_TRUE(p1 == p2);
    EXPECT_FALSE(p1 == p3);
}

TEST_F(ImageUtilTest, ToIndexConversion) {
    PointXy p(3.6, 7.4);
    int x_index, y_index;
    p.ToIndex(x_index, y_index);
    
    EXPECT_EQ(x_index, 4);  // std::round(3.6) == 4
    EXPECT_EQ(y_index, 7);  // std::round(7.4) == 7
}

TEST_F(ImageUtilTest, InImage) {
    PointXy p1(4.5, 5.5);
    PointXy p2(-1.0, 3.0);
    PointXy p3(10.9, 10.9);

    EXPECT_TRUE(p1.InImage(10, 10));  // Should be inside
    EXPECT_FALSE(p2.InImage(10, 10)); // Negative x should be outside
    EXPECT_FALSE(p3.InImage(10, 10)); // Exceeds max range
}

TEST_F(ImageUtilTest, DefaultAxesInfoConstructor) {
    AxesInfo axes;
    EXPECT_EQ(axes.x, -1);
    EXPECT_EQ(axes.y, -1);
    EXPECT_EQ(axes.spatial_x, -1);
    EXPECT_EQ(axes.spatial_y, -1);
    EXPECT_EQ(axes.spectral, -1);
    EXPECT_EQ(axes.z, -1);
    EXPECT_EQ(axes.stokes, -1);
}

TEST_F(ImageUtilTest, ConstructorWithRenderAndSpatial) {
    std::vector<int> render = {2, 3};
    std::vector<int> spatial = {4, 5};
    int spectral = 6;

    AxesInfo axes(render, spatial, spectral);

    EXPECT_EQ(axes.x, 2);
    EXPECT_EQ(axes.y, 3);
    EXPECT_EQ(axes.spatial_x, 4);
    EXPECT_EQ(axes.spatial_y, 5);
    EXPECT_EQ(axes.spectral, 6);
    EXPECT_EQ(axes.z, -1);  // Default value
    EXPECT_EQ(axes.stokes, -1);  // Default value
}

TEST_F(ImageUtilTest, ConstructorWithRenderSpatialSpectralZStokes) {
    std::vector<int> render = {1, 2};
    std::vector<int> spatial = {3, 4};
    int spectral = 5;
    int z = 6;
    int stokes = 7;

    AxesInfo axes(render, spatial, spectral, z, stokes);

    EXPECT_EQ(axes.x, 1);
    EXPECT_EQ(axes.y, 2);
    EXPECT_EQ(axes.spatial_x, 3);
    EXPECT_EQ(axes.spatial_y, 4);
    EXPECT_EQ(axes.spectral, 5);
    EXPECT_EQ(axes.z, 6);
    EXPECT_EQ(axes.stokes, 7);
}

TEST_F(ImageUtilTest, RenderMethod) {
    AxesInfo axes({10, 20}, {30, 40}, 50);
    std::vector<int> render = axes.Render();
    
    EXPECT_EQ(render.size(), 2);
    EXPECT_EQ(render[0], 10);
    EXPECT_EQ(render[1], 20);
}

TEST_F(ImageUtilTest, SpatialMethod) {
    AxesInfo axes({10, 20}, {30, 40}, 50);
    std::vector<int> spatial = axes.Spatial();
    
    EXPECT_EQ(spatial.size(), 2);
    EXPECT_EQ(spatial[0], 30);
    EXPECT_EQ(spatial[1], 40);
}

// Edge case: Ensure constructor handles empty vectors safely
// TEST_F(ImageUtilTest, ConstructorWithEmptyVectors) {
//     std::vector<int> empty_render;
//     std::vector<int> empty_spatial;
    
//     EXPECT_THROW(AxesInfo axes(empty_render, empty_spatial, 5), std::out_of_range);
// }

// Edge case: Ensure constructor handles one-element vectors safely
// TEST_F(ImageUtilTest, ConstructorWithSingleElementVectors) {
//     std::vector<int> render = {5};
//     std::vector<int> spatial = {10};
    
//     EXPECT_THROW(AxesInfo axes(render, spatial, 15), std::out_of_range);
// }

// Edge case: Ensure constructor handles oversized vectors safely
TEST_F(ImageUtilTest, ConstructorWithOversizedVectors) {
    std::vector<int> render = {1, 2, 3};  // Extra element
    std::vector<int> spatial = {4, 5, 6};  // Extra element
    int spectral = 7;
    
    AxesInfo axes(render, spatial, spectral);
    
    EXPECT_EQ(axes.x, 1);  // Should still take first element
    EXPECT_EQ(axes.y, 2);
    EXPECT_EQ(axes.spatial_x, 4);
    EXPECT_EQ(axes.spatial_y, 5);
}

// Edge case: Ensure Render() returns correct values when struct is uninitialized
TEST_F(ImageUtilTest, RenderMethodOnDefault) {
    AxesInfo axes;
    std::vector<int> render = axes.Render();
    
    EXPECT_EQ(render.size(), 2);
    EXPECT_EQ(render[0], -1);
    EXPECT_EQ(render[1], -1);
}

// Edge case: Ensure Spatial() returns correct values when struct is uninitialized
TEST_F(ImageUtilTest, SpatialMethodOnDefault) {
    AxesInfo axes;
    std::vector<int> spatial = axes.Spatial();
    
    EXPECT_EQ(spatial.size(), 2);
    EXPECT_EQ(spatial[0], -1);
    EXPECT_EQ(spatial[1], -1);
}

TEST_F(ImageUtilTest, DefaultDimsInfoConstructor) {
    DimsInfo dims;
    EXPECT_EQ(dims.width, 1);
    EXPECT_EQ(dims.height, 1);
    EXPECT_EQ(dims.depth, 1);
    EXPECT_EQ(dims.num_channels, 1);
    EXPECT_EQ(dims.num_stokes, 1);
}

TEST_F(ImageUtilTest, FromAxisValidIndex) {
    casacore::IPosition shape(5, 10, 20, 30, 40, 50);  // Example shape
    EXPECT_EQ(DimsInfo::FromAxis(0, shape), 10);
    EXPECT_EQ(DimsInfo::FromAxis(1, shape), 20);
    EXPECT_EQ(DimsInfo::FromAxis(2, shape), 30);
    EXPECT_EQ(DimsInfo::FromAxis(3, shape), 40);
    EXPECT_EQ(DimsInfo::FromAxis(4, shape), 50);
}

TEST_F(ImageUtilTest, FromAxisNegativeIndex) {
    casacore::IPosition shape(5, 10, 20, 30, 40, 50);
    EXPECT_EQ(DimsInfo::FromAxis(-1, shape), 1);  // Negative index should return 1
    EXPECT_EQ(DimsInfo::FromAxis(-5, shape), 1);
}

// TEST_F(ImageUtilTest, ConstructorWithAxesInfo) {
//     casacore::IPosition shape(5, 100, 200, 300, 400, 500);
//     AxesInfo axes({0, 1}, {2, 3}, 4, -1, 5);  // x=0, y=1, z=-1, spectral=4, stokes=5

//     DimsInfo dims(axes, shape);

//     EXPECT_EQ(dims.width, 100);  // x-axis
//     EXPECT_EQ(dims.height, 200); // y-axis
//     EXPECT_EQ(dims.depth, 1);    // z=-1, should default to 1
//     EXPECT_EQ(dims.num_channels, 400);  // spectral=4
//     EXPECT_EQ(dims.num_stokes, 500);    // stokes=5
// }

// TEST_F(ImageUtilTest, ConstructorWithInvalidAxes) {
//     casacore::IPosition shape(3, 10, 20, 30); // Only 3 dimensions

//     AxesInfo axes({0, 1}, {2, 3}, 4, 5, 6); // Out-of-range indices

//     EXPECT_THROW(DimsInfo dims(axes, shape), casacore::AipsError);
// }

// TEST_F(ImageUtilTest, LargeShape) {
//     casacore::IPosition shape(5, 1000, 2000, 3000, 4000, 5000);
//     AxesInfo axes({1, 2}, {3, 4}, 0, 1, 2);

//     DimsInfo dims(axes, shape);

//     EXPECT_EQ(dims.width, 2000);
//     EXPECT_EQ(dims.height, 3000);
//     EXPECT_EQ(dims.depth, 1000);
//     EXPECT_EQ(dims.num_channels, 1000);
//     EXPECT_EQ(dims.num_stokes, 2000);
// }
