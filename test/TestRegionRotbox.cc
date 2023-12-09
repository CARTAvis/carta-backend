/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include "CommonTestUtilities.h"
#include "ImageData/FileLoader.h"
#include "Region/Region.h"
#include "Region/RegionHandler.h"
#include "src/Frame/Frame.h"

using namespace carta;

using ::testing::FloatNear;
using ::testing::Pointwise;

class RegionRotboxTest : public ::testing::Test {
public:
    static bool SetRegion(carta::RegionHandler& region_handler, int file_id, int& region_id, CARTA::RegionType type,
        const std::vector<float>& points, float rotation, std::shared_ptr<casacore::CoordinateSystem> csys) {
        std::vector<CARTA::Point> control_points;
        for (auto i = 0; i < points.size(); i += 2) {
            control_points.push_back(Message::Point(points[i], points[i + 1]));
        }

        // Define RegionState and set region (region_id updated)
        RegionState region_state(file_id, type, control_points, rotation);
        return region_handler.SetRegion(region_id, region_state, csys);
    }

    static void ConvertRotboxPointsToCorners(
        const std::vector<float>& points, float rotation, std::vector<float>& x, std::vector<float>& y) {
        float center_x(points[0]), center_y(points[1]);
        float width(points[2]), height(points[3]);
        float cos_x = cos(rotation * M_PI / 180.0f);
        float sin_x = sin(rotation * M_PI / 180.0f);
        float width_vector_x = cos_x * width;
        float width_vector_y = sin_x * width;
        float height_vector_x = -sin_x * height;
        float height_vector_y = cos_x * height;

        // Bottom left
        x.push_back(center_x + (-width_vector_x - height_vector_x) / 2.0f);
        y.push_back(center_y + (-width_vector_y - height_vector_y) / 2.0f);
        // Bottom right
        x.push_back(center_x + (width_vector_x - height_vector_x) / 2.0f);
        y.push_back(center_y + (width_vector_y - height_vector_y) / 2.0f);
        // Top right
        x.push_back(center_x + (width_vector_x + height_vector_x) / 2.0f);
        y.push_back(center_y + (width_vector_y + height_vector_y) / 2.0f);
        // Top left
        x.push_back(center_x + (-width_vector_x + height_vector_x) / 2.0f);
        y.push_back(center_y + (-width_vector_y + height_vector_y) / 2.0f);
    }
};

TEST_F(RegionRotboxTest, TestReferenceImageRotboxLCRegion) {
    std::string image_path = FileFinder::FitsImagePath("noise_3d.fits"); // 10x10x10
    std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(image_path));
    std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));

    // Set region
    carta::RegionHandler region_handler;
    int file_id(0), region_id(-1);
    CARTA::RegionType region_type = CARTA::RegionType::RECTANGLE;
    std::vector<float> points = {5.0, 5.0, 4.0, 3.0};
    float rotation(30.0);
    auto csys = frame->CoordinateSystem();
    bool ok = SetRegion(region_handler, file_id, region_id, region_type, points, rotation, csys);
    ASSERT_TRUE(ok);

    // Get Region as 3D LCRegion
    auto region = region_handler.GetRegion(region_id);
    ASSERT_TRUE(region); // shared_ptr<Region>
    auto image_shape = frame->ImageShape();
    auto lc_region = region->GetImageRegion(file_id, csys, image_shape);
    ASSERT_TRUE(lc_region); // shared_ptr<casacore::LCRegion>
    ASSERT_EQ(lc_region->ndim(), image_shape.size());
    ASSERT_EQ(lc_region->latticeShape(), image_shape);
    ASSERT_EQ(lc_region->shape(), casacore::IPosition(3, 5, 5, 10));
}

TEST_F(RegionRotboxTest, TestReferenceImageRotboxRecord) {
    std::string image_path = FileFinder::FitsImagePath("noise_3d.fits"); // 10x10x10
    std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(image_path));
    std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));

    // Set region
    carta::RegionHandler region_handler;
    int file_id(0), region_id(-1);
    CARTA::RegionType region_type = CARTA::RegionType::RECTANGLE;
    std::vector<float> points = {5.0, 5.0, 4.0, 3.0};
    float rotation(30.0);
    auto csys = frame->CoordinateSystem();
    bool ok = SetRegion(region_handler, file_id, region_id, region_type, points, rotation, csys);
    ASSERT_TRUE(ok);

    // Get Region as casacore::Record
    auto region = region_handler.GetRegion(region_id);
    ASSERT_TRUE(region); // shared_ptr<Region>
    auto image_shape = frame->ImageShape();
    auto region_record = region->GetImageRegionRecord(file_id, csys, image_shape);
    ASSERT_GT(region_record.nfields(), 0);
    ASSERT_EQ(region_record.asInt("isRegion"), 1);
    ASSERT_EQ(region_record.asString("name"), "LCPolygon"); // box corners set as polygon
    ASSERT_TRUE(region_record.asBool("oneRel"));            // FITS 1-based
    // x, y order is [blc, brc, trc, tlc, blc]
    auto x = region_record.asArrayFloat("x").tovector();
    auto y = region_record.asArrayFloat("y").tovector();
    std::vector<float> expected_x, expected_y;
    ConvertRotboxPointsToCorners(points, rotation, expected_x, expected_y);
    ASSERT_EQ(x.size(), 5); // repeats first point to close polygon
    ASSERT_EQ(y.size(), 5); // repeats first point to close polygon
    ASSERT_FLOAT_EQ(x[0], expected_x[0] + 1.0);
    ASSERT_FLOAT_EQ(x[1], expected_x[1] + 1.0);
    ASSERT_FLOAT_EQ(x[2], expected_x[2] + 1.0);
    ASSERT_FLOAT_EQ(x[3], expected_x[3] + 1.0);
    ASSERT_FLOAT_EQ(x[4], expected_x[0] + 1.0);
    ASSERT_FLOAT_EQ(y[0], expected_y[0] + 1.0);
    ASSERT_FLOAT_EQ(y[1], expected_y[1] + 1.0);
    ASSERT_FLOAT_EQ(y[2], expected_y[2] + 1.0);
    ASSERT_FLOAT_EQ(y[3], expected_y[3] + 1.0);
    ASSERT_FLOAT_EQ(y[4], expected_y[0] + 1.0);
}

TEST_F(RegionRotboxTest, TestMatchedImageRotboxLCRegion) {
    // frame 0
    std::string image_path0 = FileFinder::FitsImagePath("noise_10px_10px.fits");
    std::shared_ptr<carta::FileLoader> loader0(carta::FileLoader::GetLoader(image_path0));
    std::shared_ptr<Frame> frame0(new Frame(0, loader0, "0"));
    // frame 1
    std::string image_path1 = FileFinder::Hdf5ImagePath("noise_10px_10px.hdf5");
    std::shared_ptr<carta::FileLoader> loader1(carta::FileLoader::GetLoader(image_path1));
    std::shared_ptr<Frame> frame1(new Frame(0, loader1, "0"));

    // Set rectangle in frame 0
    carta::RegionHandler region_handler;
    int file_id(0), region_id(-1);
    CARTA::RegionType region_type = CARTA::RegionType::RECTANGLE;
    std::vector<float> points = {5.0, 5.0, 4.0, 3.0};
    float rotation(30.0);
    auto csys = frame0->CoordinateSystem();
    bool ok = SetRegion(region_handler, file_id, region_id, region_type, points, rotation, csys);
    ASSERT_TRUE(ok);
    auto region = region_handler.GetRegion(region_id);
    ASSERT_TRUE(region); // shared_ptr<Region>

    // Get Region as 2D LCRegion in frame1
    file_id = 1;
    csys = frame1->CoordinateSystem();
    auto image_shape = frame1->ImageShape();
    auto lc_region = region->GetImageRegion(file_id, csys, image_shape);

    // Check LCRegion
    ASSERT_TRUE(lc_region); // shared_ptr<casacore::LCRegion>
    ASSERT_EQ(lc_region->ndim(), image_shape.size());
    ASSERT_EQ(lc_region->latticeShape(), image_shape);
    ASSERT_EQ(lc_region->shape(), casacore::IPosition(2, 5, 5));
}

TEST_F(RegionRotboxTest, TestMatchedImageRotboxRecord) {
    // frame 0
    std::string image_path0 = FileFinder::FitsImagePath("noise_10px_10px.fits");
    std::shared_ptr<carta::FileLoader> loader0(carta::FileLoader::GetLoader(image_path0));
    std::shared_ptr<Frame> frame0(new Frame(0, loader0, "0"));
    // frame 1
    std::string image_path1 = FileFinder::Hdf5ImagePath("noise_10px_10px.hdf5");
    std::shared_ptr<carta::FileLoader> loader1(carta::FileLoader::GetLoader(image_path1));
    std::shared_ptr<Frame> frame1(new Frame(0, loader1, "0"));

    // Set region in frame0
    carta::RegionHandler region_handler;
    int file_id(0), region_id(-1);
    CARTA::RegionType region_type = CARTA::RegionType::RECTANGLE;
    std::vector<float> points = {5.0, 5.0, 4.0, 3.0};
    float rotation(30.0);
    auto csys = frame0->CoordinateSystem();
    bool ok = SetRegion(region_handler, file_id, region_id, region_type, points, rotation, csys);
    ASSERT_TRUE(ok);
    auto region = region_handler.GetRegion(region_id);
    ASSERT_TRUE(region); // shared_ptr<Region>

    // Get Region as casacore::Record in frame1
    file_id = 1;
    csys = frame1->CoordinateSystem();
    auto image_shape = frame1->ImageShape();
    auto region_record = region->GetImageRegionRecord(file_id, csys, image_shape);

    // Check record
    ASSERT_GT(region_record.nfields(), 0);
    ASSERT_EQ(region_record.asInt("isRegion"), 1);
    ASSERT_EQ(region_record.asString("name"), "LCPolygon"); // box corners set as polygon
    ASSERT_FALSE(region_record.asBool("oneRel"));
    std::cerr << "matched rotbox record=" << region_record << std::endl;
    // x, y order is [blc, brc, trc, tlc, blc]
    auto x = region_record.asArrayFloat("x").tovector();
    auto y = region_record.asArrayFloat("y").tovector();
    // keep original rectangle pixel points with rotation for export
    float left_x = points[0] - (points[2] / 2.0);
    float right_x = points[0] + (points[2] / 2.0);
    float bottom_y = points[1] - (points[3] / 2.0);
    float top_y = points[1] + (points[3] / 2.0);
    ASSERT_EQ(x.size(), 4);
    ASSERT_EQ(y.size(), 4);
    ASSERT_FLOAT_EQ(x[0], left_x);
    ASSERT_FLOAT_EQ(x[1], right_x);
    ASSERT_FLOAT_EQ(x[2], right_x);
    ASSERT_FLOAT_EQ(x[3], left_x);
    ASSERT_FLOAT_EQ(y[0], bottom_y);
    ASSERT_FLOAT_EQ(y[1], bottom_y);
    ASSERT_FLOAT_EQ(y[2], top_y);
    ASSERT_FLOAT_EQ(y[3], top_y);
}
