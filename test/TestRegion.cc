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

class RegionTest : public ::testing::Test {
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
};

TEST_F(RegionTest, TestSetUpdateRemoveRegion) {
    std::string image_path = FileFinder::FitsImagePath("noise_3d.fits");
    std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(image_path));
    std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));

    // Set region
    carta::RegionHandler region_handler;
    int file_id(0), region_id(-1);
    CARTA::RegionType region_type = CARTA::RegionType::RECTANGLE;
    std::vector<float> points = {5.0, 5.0, 4.0, 3.0};
    float rotation(0.0);
    auto csys = frame->CoordinateSystem();
    bool ok = SetRegion(region_handler, file_id, region_id, region_type, points, rotation, csys);
    // RegionHandler checks
    ASSERT_TRUE(ok);
    ASSERT_FALSE(region_handler.IsPointRegion(region_id));
    ASSERT_FALSE(region_handler.IsLineRegion(region_id));
    ASSERT_TRUE(region_handler.IsClosedRegion(region_id));

    // Region checks
    auto region = region_handler.GetRegion(region_id);
    ASSERT_TRUE(region); // shared_ptr<Region>
    ASSERT_TRUE(region->IsValid());
    ASSERT_FALSE(region->IsPoint());
    ASSERT_FALSE(region->IsLineType());
    ASSERT_FALSE(region->IsRotbox());
    ASSERT_FALSE(region->IsAnnotation());
    ASSERT_FALSE(region->RegionChanged());
    ASSERT_TRUE(region->IsConnected());
    ASSERT_EQ(region->CoordinateSystem(), csys);
    auto region_state = region->GetRegionState();

    // Update region
    rotation = 30.0;
    ok = SetRegion(region_handler, file_id, region_id, region_type, points, rotation, csys);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(region->IsValid());
    ASSERT_TRUE(region->RegionChanged());
    ASSERT_TRUE(region->IsRotbox());
    auto new_region_state = region->GetRegionState();
    ASSERT_FALSE(region_state == new_region_state);

    // Remove region and frame (not set, should not cause error)
    region_handler.RemoveRegion(region_id);
    auto no_region = region_handler.GetRegion(region_id);
    ASSERT_FALSE(no_region);
}

TEST_F(RegionTest, TestReferenceImageRectangleLCRegion) {
    std::string image_path = FileFinder::FitsImagePath("noise_3d.fits");
    std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(image_path));
    std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));

    // Set region
    carta::RegionHandler region_handler;
    int file_id(0), region_id(-1);
    CARTA::RegionType region_type = CARTA::RegionType::RECTANGLE;
    std::vector<float> points = {5.0, 5.0, 4.0, 3.0};
    float rotation(0.0);
    auto csys = frame->CoordinateSystem();
    bool ok = SetRegion(region_handler, file_id, region_id, region_type, points, rotation, csys);
    ASSERT_TRUE(ok);
    // Get Region as 3D LCRegion
    auto region = region_handler.GetRegion(region_id);
    ASSERT_TRUE(region); // shared_ptr<Region>
    auto image_shape = frame->ImageShape();
    auto lc_region = region->GetImageRegion(file_id, csys, image_shape);
    ASSERT_TRUE(lc_region); // shared_ptr<casacore::LCRegion>
    ASSERT_EQ(lc_region->ndim(), 2);
    ASSERT_EQ(lc_region->latticeShape()(0), image_shape(0));
    ASSERT_EQ(lc_region->latticeShape()(1), image_shape(1));
    ASSERT_EQ(lc_region->shape(), casacore::IPosition(2, 5, 3));
}

TEST_F(RegionTest, TestReferenceImageRotboxLCRegion) {
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
    ASSERT_EQ(lc_region->ndim(), 2);
    ASSERT_EQ(lc_region->latticeShape()(0), image_shape(0));
    ASSERT_EQ(lc_region->latticeShape()(1), image_shape(1));
    ASSERT_EQ(lc_region->shape(), casacore::IPosition(2, 5, 5));
}

TEST_F(RegionTest, TestReferenceImageEllipseLCRegion) {
    std::string image_path = FileFinder::FitsImagePath("noise_3d.fits");
    std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(image_path));
    std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));

    // Set region
    carta::RegionHandler region_handler;
    int file_id(0), region_id(-1);
    CARTA::RegionType region_type = CARTA::RegionType::ELLIPSE;
    std::vector<float> points = {5.0, 5.0, 4.0, 3.0};
    float rotation(0.0);
    auto csys = frame->CoordinateSystem();
    bool ok = SetRegion(region_handler, file_id, region_id, region_type, points, rotation, csys);
    ASSERT_TRUE(ok);

    // Get Region as 3D LCRegion
    auto region = region_handler.GetRegion(region_id);
    ASSERT_TRUE(region); // shared_ptr<Region>
    auto image_shape = frame->ImageShape();
    auto lc_region = region->GetImageRegion(file_id, csys, image_shape);
    ASSERT_TRUE(lc_region); // shared_ptr<casacore::LCRegion>
    ASSERT_EQ(lc_region->ndim(), 2);
    ASSERT_EQ(lc_region->latticeShape()(0), image_shape(0));
    ASSERT_EQ(lc_region->latticeShape()(1), image_shape(1));
    ASSERT_EQ(lc_region->shape(), casacore::IPosition(2, 7, 9));
}

TEST_F(RegionTest, TestReferenceImagePolygonLCRegion) {
    std::string image_path = FileFinder::FitsImagePath("noise_3d.fits");
    std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(image_path));
    std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));

    // Set region
    carta::RegionHandler region_handler;
    int file_id(0), region_id(-1);
    CARTA::RegionType region_type = CARTA::RegionType::POLYGON;
    std::vector<float> points = {5.0, 5.0, 4.0, 3.0, 1.0, 6.0, 3.0, 8.0};
    float rotation(0.0);
    auto csys = frame->CoordinateSystem();
    bool ok = SetRegion(region_handler, file_id, region_id, region_type, points, rotation, csys);
    ASSERT_TRUE(ok);

    // Get Region as 3D LCRegion
    auto region = region_handler.GetRegion(region_id);
    ASSERT_TRUE(region); // shared_ptr<Region>
    auto image_shape = frame->ImageShape();
    auto lc_region = region->GetImageRegion(file_id, csys, image_shape);
    ASSERT_TRUE(lc_region); // shared_ptr<casacore::LCRegion>
    ASSERT_EQ(lc_region->ndim(), 2);
    ASSERT_EQ(lc_region->latticeShape()(0), image_shape(0));
    ASSERT_EQ(lc_region->latticeShape()(1), image_shape(1));
    ASSERT_EQ(lc_region->shape(), casacore::IPosition(2, 5, 6));
}

TEST_F(RegionTest, TestReferenceImagePointRecord) {
    std::string image_path = FileFinder::FitsImagePath("noise_3d.fits");
    std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(image_path));
    std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));

    // Set region
    carta::RegionHandler region_handler;
    int file_id(0), region_id(-1);
    CARTA::RegionType region_type = CARTA::RegionType::POINT;
    std::vector<float> points = {4.0, 2.0};
    float rotation(0.0);
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
    ASSERT_EQ(region_record.asString("name"), "LCBox");
    ASSERT_FALSE(region_record.asBool("oneRel"));
    auto blc = region_record.asArrayFloat("blc").tovector();
    auto trc = region_record.asArrayFloat("trc").tovector();
    ASSERT_EQ(blc.size(), 3);
    ASSERT_EQ(trc.size(), 3);
    ASSERT_FLOAT_EQ(blc[0], points[0]);
    ASSERT_FLOAT_EQ(blc[1], points[1]);
    ASSERT_FLOAT_EQ(blc[2], 0.0); // channel
    ASSERT_FLOAT_EQ(trc[0], points[0]);
    ASSERT_FLOAT_EQ(trc[1], points[1]);
    ASSERT_FLOAT_EQ(trc[2], 0.0); // channel
}

TEST_F(RegionTest, TestReferenceImageLineRecord) {
    std::string image_path = FileFinder::FitsImagePath("noise_3d.fits");
    std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(image_path));
    std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));

    // Set region
    carta::RegionHandler region_handler;
    int file_id(0), region_id(-1);
    CARTA::RegionType region_type = CARTA::RegionType::LINE;
    std::vector<float> points = {5.0, 5.0, 4.0, 3.0};
    float rotation(0.0);
    auto csys = frame->CoordinateSystem();
    bool ok = SetRegion(region_handler, file_id, region_id, region_type, points, rotation, csys);
    ASSERT_TRUE(ok);

    // Get Region as casacore::Record (from control points, no casacore Line region)
    auto region = region_handler.GetRegion(region_id);
    ASSERT_TRUE(region); // shared_ptr<Region>
    auto image_shape = frame->ImageShape();
    auto region_record = region->GetImageRegionRecord(file_id, csys, image_shape);
    ASSERT_GT(region_record.nfields(), 0);
    ASSERT_EQ(region_record.asInt("isRegion"), 1);
    ASSERT_EQ(region_record.asString("name"), "Line");
    ASSERT_FALSE(region_record.asBool("oneRel"));
    auto x = region_record.asArrayFloat("x").tovector();
    auto y = region_record.asArrayFloat("y").tovector();
    ASSERT_EQ(x.size(), 2);
    ASSERT_EQ(y.size(), 2);
    ASSERT_FLOAT_EQ(x[0], points[0]);
    ASSERT_FLOAT_EQ(x[1], points[2]);
    ASSERT_FLOAT_EQ(y[0], points[1]);
    ASSERT_FLOAT_EQ(y[1], points[3]);
}

TEST_F(RegionTest, TestReferenceImageRectangleRecord) {
    std::string image_path = FileFinder::FitsImagePath("noise_3d.fits");
    std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(image_path));
    std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));

    // Set region
    carta::RegionHandler region_handler;
    int file_id(0), region_id(-1);
    CARTA::RegionType region_type = CARTA::RegionType::RECTANGLE;
    std::vector<float> points = {5.0, 5.0, 4.0, 3.0};
    float rotation(0.0);
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
    ASSERT_FALSE(region_record.asBool("oneRel"));
    // x, y order is [blc, brc, trc, tlc, blc]
    auto x = region_record.asArrayFloat("x").tovector();
    auto y = region_record.asArrayFloat("y").tovector();
    float left_x = points[0] - (points[2] / 2.0);
    float right_x = points[0] + (points[2] / 2.0);
    float bottom_y = points[1] - (points[3] / 2.0);
    float top_y = points[1] + (points[3] / 2.0);
    ASSERT_EQ(x.size(), 5); // casacore repeats first point to close polygon
    ASSERT_EQ(y.size(), 5); // casacore repeats first point to close polygon
    ASSERT_FLOAT_EQ(x[0], left_x);
    ASSERT_FLOAT_EQ(x[1], right_x);
    ASSERT_FLOAT_EQ(x[2], right_x);
    ASSERT_FLOAT_EQ(x[3], left_x);
    ASSERT_FLOAT_EQ(x[4], left_x);
    ASSERT_FLOAT_EQ(y[0], bottom_y);
    ASSERT_FLOAT_EQ(y[1], bottom_y);
    ASSERT_FLOAT_EQ(y[2], top_y);
    ASSERT_FLOAT_EQ(y[3], top_y);
    ASSERT_FLOAT_EQ(y[4], bottom_y);
}

TEST_F(RegionTest, TestReferenceImageRotboxRecord) {
    // Record is for unrotated rectangle; RegionState used for angle in export
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
    ASSERT_FALSE(region_record.asBool("oneRel"));
    // x, y order is [blc, brc, trc, tlc, blc]
    auto x = region_record.asArrayFloat("x").tovector();
    auto y = region_record.asArrayFloat("y").tovector();
    float left_x = points[0] - (points[2] / 2.0);
    float right_x = points[0] + (points[2] / 2.0);
    float bottom_y = points[1] - (points[3] / 2.0);
    float top_y = points[1] + (points[3] / 2.0);
    ASSERT_EQ(x.size(), 5); // repeats first point to close polygon
    ASSERT_EQ(y.size(), 5); // repeats first point to close polygon
    ASSERT_FLOAT_EQ(x[0], left_x);
    ASSERT_FLOAT_EQ(x[1], right_x);
    ASSERT_FLOAT_EQ(x[2], right_x);
    ASSERT_FLOAT_EQ(x[3], left_x);
    ASSERT_FLOAT_EQ(x[4], left_x);
    ASSERT_FLOAT_EQ(y[0], bottom_y);
    ASSERT_FLOAT_EQ(y[1], bottom_y);
    ASSERT_FLOAT_EQ(y[2], top_y);
    ASSERT_FLOAT_EQ(y[3], top_y);
    ASSERT_FLOAT_EQ(y[4], bottom_y);
}

TEST_F(RegionTest, TestReferenceImageEllipseRecord) {
    std::string image_path = FileFinder::FitsImagePath("noise_3d.fits");
    std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(image_path));
    std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));

    // Set region
    carta::RegionHandler region_handler;
    int file_id(0), region_id(-1);
    CARTA::RegionType region_type = CARTA::RegionType::ELLIPSE;
    std::vector<float> points = {5.0, 5.0, 4.0, 3.0};
    float rotation(0.0);
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
    ASSERT_EQ(region_record.asString("name"), "LCEllipsoid");
    ASSERT_FALSE(region_record.asBool("oneRel"));
    auto center = region_record.asArrayFloat("center").tovector();
    ASSERT_FLOAT_EQ(center[0], points[0]);
    ASSERT_FLOAT_EQ(center[1], points[1]);
    auto radii = region_record.asArrayFloat("radii").tovector();
    ASSERT_FLOAT_EQ(radii[0], points[2]);
    ASSERT_FLOAT_EQ(radii[1], points[3]);
}

TEST_F(RegionTest, TestReferenceImagePolygonRecord) {
    std::string image_path = FileFinder::FitsImagePath("noise_3d.fits");
    std::shared_ptr<carta::FileLoader> loader(carta::FileLoader::GetLoader(image_path));
    std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));

    // Set region
    carta::RegionHandler region_handler;
    int file_id(0), region_id(-1);
    CARTA::RegionType region_type = CARTA::RegionType::POLYGON;
    std::vector<float> points = {5.0, 5.0, 4.0, 3.0, 1.0, 6.0, 3.0, 8.0}; // 4 points
    float rotation(0.0);
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
    ASSERT_EQ(region_record.asString("name"), "LCPolygon");
    ASSERT_FALSE(region_record.asBool("oneRel"));
    // x, y points
    auto x = region_record.asArrayFloat("x").tovector();
    auto y = region_record.asArrayFloat("y").tovector();
    ASSERT_EQ(x.size(), 5); // casacore repeats first point to close polygon
    ASSERT_EQ(y.size(), 5); // casacore repeats first point to close polygon
    ASSERT_FLOAT_EQ(x[0], points[0]);
    ASSERT_FLOAT_EQ(x[1], points[2]);
    ASSERT_FLOAT_EQ(x[2], points[4]);
    ASSERT_FLOAT_EQ(x[3], points[6]);
    ASSERT_FLOAT_EQ(y[0], points[1]);
    ASSERT_FLOAT_EQ(y[1], points[3]);
    ASSERT_FLOAT_EQ(y[2], points[5]);
    ASSERT_FLOAT_EQ(y[3], points[7]);
}
