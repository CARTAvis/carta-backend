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

class RegionMatchedTest : public ::testing::Test {
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

TEST_F(RegionMatchedTest, TestMatchedImageRectangleLCRegion) {
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
    float rotation(0.0);
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
    ASSERT_EQ(lc_region->shape(), casacore::IPosition(2, 5, 3));
}

TEST_F(RegionMatchedTest, TestMatchedImageRotboxLCRegion) {
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

TEST_F(RegionMatchedTest, TestMatchedImageEllipseLCRegion) {
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
    CARTA::RegionType region_type = CARTA::RegionType::ELLIPSE;
    std::vector<float> points = {5.0, 5.0, 4.0, 3.0};
    float rotation(0.0);
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
    ASSERT_EQ(lc_region->shape(), casacore::IPosition(2, 7, 9));
}

TEST_F(RegionMatchedTest, TestMatchedImagePolygonLCRegion) {
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
    CARTA::RegionType region_type = CARTA::RegionType::POLYGON;
    std::vector<float> points = {5.0, 5.0, 4.0, 3.0, 1.0, 6.0, 3.0, 8.0};
    float rotation(0.0);
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
    ASSERT_EQ(lc_region->shape(), casacore::IPosition(2, 5, 6));
}

TEST_F(RegionMatchedTest, TestMatchedImagePointRecord) {
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
    CARTA::RegionType region_type = CARTA::RegionType::POINT;
    std::vector<float> points = {4.0, 2.0};
    float rotation(0.0);
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
    ASSERT_EQ(region_record.asString("name"), "LCBox"); // box with blc = trc
    ASSERT_TRUE(region_record.asBool("oneRel"));        // 1-based pixels
    auto blc = region_record.asArrayFloat("blc").tovector();
    auto trc = region_record.asArrayFloat("trc").tovector();
    ASSERT_EQ(blc.size(), 2);
    ASSERT_EQ(trc.size(), 2);
    ASSERT_FLOAT_EQ(blc[0], points[0] + 1.0);
    ASSERT_FLOAT_EQ(blc[1], points[1] + 1.0);
    ASSERT_FLOAT_EQ(trc[0], points[0] + 1.0);
    ASSERT_FLOAT_EQ(trc[1], points[1] + 1.0);
}

TEST_F(RegionMatchedTest, TestMatchedImageLineRecord) {
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
    CARTA::RegionType region_type = CARTA::RegionType::LINE;
    std::vector<float> points = {5.0, 5.0, 4.0, 3.0};
    float rotation(0.0);
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
    ASSERT_EQ(region_record.asString("name"), "line");
    ASSERT_FALSE(region_record.asBool("oneRel"));
    auto x = region_record.asArrayDouble("x").tovector();
    auto y = region_record.asArrayDouble("y").tovector();
    ASSERT_EQ(x.size(), 2);
    ASSERT_EQ(y.size(), 2);
    ASSERT_FLOAT_EQ(x[0], points[0]);
    ASSERT_FLOAT_EQ(x[1], points[2]);
    ASSERT_FLOAT_EQ(y[0], points[1]);
    ASSERT_FLOAT_EQ(y[1], points[3]);
}

TEST_F(RegionMatchedTest, TestMatchedImageRectangleRecord) {
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
    float rotation(0.0);
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
    ASSERT_TRUE(region_record.asBool("oneRel"));            // 1-based pixels
    // x, y order is [blc, brc, trc, tlc, blc]
    auto x = region_record.asArrayFloat("x").tovector();
    auto y = region_record.asArrayFloat("y").tovector();
    float left_x = points[0] - (points[2] / 2.0) + 1.0;
    float right_x = points[0] + (points[2] / 2.0) + 1.0;
    float bottom_y = points[1] - (points[3] / 2.0) + 1.0;
    float top_y = points[1] + (points[3] / 2.0) + 1.0;
    ASSERT_EQ(x.size(), 5); // casacore repeats first point to close polygon
    ASSERT_EQ(y.size(), 5); // casacore repeats first point to close polygon
    ASSERT_FLOAT_EQ(x[0], left_x);
    ASSERT_FLOAT_EQ(x[1], right_x);
    ASSERT_FLOAT_EQ(y[0], bottom_y);
    ASSERT_FLOAT_EQ(y[2], top_y);
}

TEST_F(RegionMatchedTest, TestMatchedImageRotboxRecord) {
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

TEST_F(RegionMatchedTest, TestMatchedImageEllipseRecord) {
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
    CARTA::RegionType region_type = CARTA::RegionType::ELLIPSE;
    std::vector<float> points = {5.0, 5.0, 4.0, 3.0};
    float rotation(0.0);
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
    ASSERT_EQ(region_record.asString("name"), "LCEllipsoid");
    ASSERT_TRUE(region_record.asBool("oneRel")); // 1-based pixels
    auto center = region_record.asArrayFloat("center").tovector();
    ASSERT_FLOAT_EQ(center[0], points[0] + 1.0);
    ASSERT_FLOAT_EQ(center[1], points[1] + 1.0);
    auto radii = region_record.asArrayFloat("radii").tovector();
    ASSERT_FLOAT_EQ(radii[0], points[3]);
    ASSERT_FLOAT_EQ(radii[1], points[2]);
}

TEST_F(RegionMatchedTest, TestMatchedImagePolygonRecord) {
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
    CARTA::RegionType region_type = CARTA::RegionType::POLYGON;
    std::vector<float> points = {5.0, 5.0, 4.0, 3.0, 1.0, 6.0, 3.0, 8.0}; // 4 points
    float rotation(0.0);
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
    ASSERT_EQ(region_record.asString("name"), "LCPolygon");
    ASSERT_TRUE(region_record.asBool("oneRel")); // 1-based pixels
    // x, y points
    auto x = region_record.asArrayFloat("x").tovector();
    auto y = region_record.asArrayFloat("y").tovector();
    ASSERT_EQ(x.size(), 5); // casacore repeats first point to close polygon
    ASSERT_EQ(y.size(), 5); // casacore repeats first point to close polygon
    ASSERT_FLOAT_EQ(x[0], points[0] + 1.0);
    ASSERT_FLOAT_EQ(x[1], points[2] + 1.0);
    ASSERT_FLOAT_EQ(x[2], points[4] + 1.0);
    ASSERT_FLOAT_EQ(x[3], points[6] + 1.0);
    ASSERT_FLOAT_EQ(y[0], points[1] + 1.0);
    ASSERT_FLOAT_EQ(y[1], points[3] + 1.0);
    ASSERT_FLOAT_EQ(y[2], points[5] + 1.0);
    ASSERT_FLOAT_EQ(y[3], points[7] + 1.0);
}
