/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include <carta-protobuf/enums.pb.h>

#include "CommonTestUtilities.h"
#include "ImageData/FileLoader.h"
#include "Region/Region.h"
#include "Region/RegionHandler.h"
#include "src/Frame/Frame.h"

using namespace carta;

class RegionImportExportTest : public ::testing::Test {
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

    static int SetAllRegions(carta::RegionHandler& region_handler, int file_id, std::shared_ptr<casacore::CoordinateSystem> csys) {
        std::vector<float> point_points = {5.0, 5.0};
        std::vector<float> line_rectangle_ellipse_points = {5.0, 5.0, 4.0, 3.0};
        std::vector<float> circle_points = {5.0, 5.0, 3.0, 3.0};
        std::vector<float> poly_points = {5.0, 5.0, 4.0, 3.0, 1.0, 6.0, 3.0, 8.0};
        std::unordered_map<CARTA::RegionType, std::vector<float>> region_points = {{CARTA::POINT, point_points},
            {CARTA::LINE, line_rectangle_ellipse_points}, {CARTA::POLYLINE, poly_points}, {CARTA::RECTANGLE, line_rectangle_ellipse_points},
            {CARTA::ELLIPSE, line_rectangle_ellipse_points}, {CARTA::POLYGON, poly_points}, {CARTA::ANNPOINT, point_points},
            {CARTA::ANNLINE, line_rectangle_ellipse_points}, {CARTA::ANNPOLYLINE, poly_points},
            {CARTA::ANNRECTANGLE, line_rectangle_ellipse_points}, {CARTA::ANNELLIPSE, line_rectangle_ellipse_points},
            {CARTA::ANNPOLYGON, poly_points}, {CARTA::ANNVECTOR, line_rectangle_ellipse_points},
            {CARTA::ANNRULER, line_rectangle_ellipse_points}, {CARTA::ANNTEXT, line_rectangle_ellipse_points},
            {CARTA::ANNCOMPASS, circle_points}};
        float rotation(0.0);
        int num_regions(0);

        // Add all region types
        int region_id(-1);
        for (int i = 0; i < CARTA::RegionType_ARRAYSIZE; ++i) {
            CARTA::RegionType type = static_cast<CARTA::RegionType>(i);
            if (type == CARTA::RegionType::ANNULUS) {
                continue;
            }
            auto points = region_points[type];
            if (!SetRegion(region_handler, file_id, region_id, type, points, rotation, csys)) {
                return 0;
            }
            num_regions++;
            region_id = -1;
        }

        // Add special region types: rotbox, circle (analytical and annotation)
        rotation = 30.0;
        if (!SetRegion(region_handler, file_id, region_id, CARTA::RECTANGLE, line_rectangle_ellipse_points, rotation, csys)) {
            return 0;
        }
        num_regions++;

        region_id = -1;
        if (!SetRegion(region_handler, file_id, region_id, CARTA::ANNRECTANGLE, line_rectangle_ellipse_points, rotation, csys)) {
            return 0;
        }
        num_regions++;

        region_id = -1;
        rotation = 0.0;
        if (!SetRegion(region_handler, file_id, region_id, CARTA::ELLIPSE, circle_points, 0.0, csys)) {
            return 0;
        }
        num_regions++;

        region_id = -1;
        if (!SetRegion(region_handler, file_id, region_id, CARTA::ANNELLIPSE, circle_points, 0.0, csys)) {
            return 0;
        }
        num_regions++;

        return num_regions;
    }

    static CARTA::RegionStyle GetRegionStyle(CARTA::RegionType type) {
        bool is_annotation = type > CARTA::POLYGON;
        std::string color = is_annotation ? "#FFBA01" : "#2EE6D6";

        CARTA::RegionStyle region_style;
        region_style.set_color(color);
        region_style.set_line_width(2);

        if (is_annotation) {
            // Default fields set by frontend
            auto annotation_style = region_style.mutable_annotation_style();
            if (type == CARTA::ANNPOINT) {
                annotation_style->set_point_shape(CARTA::PointAnnotationShape::SQUARE);
                annotation_style->set_point_width(6);
            } else if (type == CARTA::ANNTEXT || type == CARTA::ANNCOMPASS || type == CARTA::ANNRULER) {
                annotation_style->set_font("Helvetica");
                annotation_style->set_font_size(20);
                annotation_style->set_font_style("Normal");
                if (type == CARTA::ANNTEXT) {
                    annotation_style->set_text_label0("Text");
                    annotation_style->set_text_position(CARTA::TextAnnotationPosition::CENTER);
                } else if (type == CARTA::ANNCOMPASS) {
                    annotation_style->set_coordinate_system("PIXEL");
                    annotation_style->set_is_east_arrow(true);
                    annotation_style->set_is_north_arrow(true);
                    annotation_style->set_text_label0("N");
                    annotation_style->set_text_label1("E");
                } else if (type == CARTA::ANNRULER) {
                    annotation_style->set_coordinate_system("PIXEL");
                }
            }
        }
        return region_style;
    }

    static std::string ConcatContents(const std::vector<string>& string_vector) {
        std::string one_string;
        for (auto& item : string_vector) {
            one_string.append(item + "\n");
        }
        return one_string;
    }
};

TEST_F(RegionImportExportTest, TestCrtfPixExportImport) {
    // frame 0
    std::string image_path0 = FileFinder::FitsImagePath("noise_10px_10px.fits");
    std::shared_ptr<carta::FileLoader> loader0(carta::FileLoader::GetLoader(image_path0));
    std::shared_ptr<Frame> frame0(new Frame(0, loader0, "0"));
    // frame 1
    std::string image_path1 = FileFinder::Hdf5ImagePath("noise_10px_10px.hdf5");
    std::shared_ptr<carta::FileLoader> loader1(carta::FileLoader::GetLoader(image_path1));
    std::shared_ptr<Frame> frame1(new Frame(0, loader1, "0"));

    // Set all region types in frame0
    carta::RegionHandler region_handler;
    int file_id(0);
    int num_regions = SetAllRegions(region_handler, file_id, frame0->CoordinateSystem());
    // All CARTA regions except ANNULUS (- 1) plus 2 rotbox and 2 circle (+ 4)
    ASSERT_EQ(num_regions, CARTA::RegionType_ARRAYSIZE - 1 + 4);

    // Set RegionStyle map for export
    std::map<int, CARTA::RegionStyle> region_style_map;
    for (int i = 0; i < num_regions; ++i) {
        int region_id = i + 1; // region 0 is cursor
        CARTA::RegionType region_type = region_handler.GetRegion(region_id)->GetRegionState().type;
        CARTA::RegionStyle style = GetRegionStyle(region_type);
        region_style_map[region_id] = style;
    }
    std::string filename; // do not export to file
    bool overwrite(false);

    // Export all regions in frame0 (reference image)
    CARTA::ExportRegionAck export_ack0;
    region_handler.ExportRegion(file_id, frame0, CARTA::CRTF, CARTA::PIXEL, region_style_map, filename, overwrite, export_ack0);
    // Check that all regions were exported
    ASSERT_EQ(export_ack0.contents_size(), num_regions + 2); // header, textbox for text

    // Import all regions in frame0 (reference image)
    std::vector<std::string> export_contents = {export_ack0.contents().begin(), export_ack0.contents().end()};
    auto contents_string = ConcatContents(export_contents);
    bool file_is_filename(false);
    CARTA::ImportRegionAck import_ack0;
    region_handler.ImportRegion(file_id, frame0, CARTA::CRTF, contents_string, file_is_filename, import_ack0);
    // Check that all regions were imported
    ASSERT_EQ(import_ack0.regions_size(), num_regions);

    // Export all regions in frame1 (matched image)
    file_id = 1;
    CARTA::ExportRegionAck export_ack1;
    region_handler.ExportRegion(file_id, frame1, CARTA::CRTF, CARTA::PIXEL, region_style_map, filename, overwrite, export_ack1);
    // Check that all regions were exported
    ASSERT_EQ(export_ack1.contents_size(), num_regions + 2); // header, textbox for text

    // Import all regions in frame1 (matched image)
    export_contents = {export_ack1.contents().begin(), export_ack1.contents().end()};
    contents_string = ConcatContents(export_contents);
    CARTA::ImportRegionAck import_ack1;
    region_handler.ImportRegion(file_id, frame1, CARTA::CRTF, contents_string, file_is_filename, import_ack1);
    // Check that all regions were imported
    ASSERT_EQ(import_ack1.regions_size(), num_regions);
}

TEST_F(RegionImportExportTest, TestCrtfWorldExportImport) {
    // frame 0
    std::string image_path0 = FileFinder::FitsImagePath("noise_10px_10px.fits");
    std::shared_ptr<carta::FileLoader> loader0(carta::FileLoader::GetLoader(image_path0));
    std::shared_ptr<Frame> frame0(new Frame(0, loader0, "0"));
    // frame 1
    std::string image_path1 = FileFinder::Hdf5ImagePath("noise_10px_10px.hdf5");
    std::shared_ptr<carta::FileLoader> loader1(carta::FileLoader::GetLoader(image_path1));
    std::shared_ptr<Frame> frame1(new Frame(0, loader1, "0"));

    // Set all region types in frame0
    carta::RegionHandler region_handler;
    int file_id(0);
    int num_regions = SetAllRegions(region_handler, file_id, frame0->CoordinateSystem());
    // All CARTA regions except ANNULUS (- 1) plus 2 rotbox and 2 circle (+ 4)
    ASSERT_EQ(num_regions, CARTA::RegionType_ARRAYSIZE - 1 + 4);

    // Export all regions in frame0 (reference image)
    std::map<int, CARTA::RegionStyle> region_style_map;
    for (int i = 0; i < num_regions; ++i) {
        int region_id = i + 1; // region 0 is cursor
        CARTA::RegionType region_type = region_handler.GetRegion(region_id)->GetRegionState().type;
        CARTA::RegionStyle style = GetRegionStyle(region_type);
        region_style_map[region_id] = style;
    }
    std::string filename; // do not export to file
    bool overwrite(false);
    CARTA::ExportRegionAck export_ack0;
    region_handler.ExportRegion(file_id, frame0, CARTA::CRTF, CARTA::WORLD, region_style_map, filename, overwrite, export_ack0);
    // Check that all regions were exported
    ASSERT_EQ(export_ack0.contents_size(), num_regions + 2); // header, textbox for text

    // Import all regions in frame0 (reference image)
    std::vector<std::string> export_contents = {export_ack0.contents().begin(), export_ack0.contents().end()};
    auto contents_string = ConcatContents(export_contents);
    bool file_is_filename(false);
    CARTA::ImportRegionAck import_ack0;
    region_handler.ImportRegion(file_id, frame0, CARTA::CRTF, contents_string, file_is_filename, import_ack0);
    // Check that all regions were imported
    ASSERT_EQ(import_ack0.regions_size(), num_regions);

    // Export all regions in frame1 (matched image)
    file_id = 1;
    CARTA::ExportRegionAck export_ack1;
    region_handler.ExportRegion(file_id, frame1, CARTA::CRTF, CARTA::WORLD, region_style_map, filename, overwrite, export_ack1);
    // Check that all regions were exported
    ASSERT_EQ(export_ack1.contents_size(), num_regions + 2); // header, textbox for text

    // Import all regions in frame1 (matched image)
    export_contents = {export_ack1.contents().begin(), export_ack1.contents().end()};
    contents_string = ConcatContents(export_contents);
    CARTA::ImportRegionAck import_ack1;
    region_handler.ImportRegion(file_id, frame1, CARTA::CRTF, contents_string, file_is_filename, import_ack1);
    // Check that all regions were imported
    ASSERT_EQ(import_ack1.regions_size(), num_regions);
}

TEST_F(RegionImportExportTest, TestDs9PixExportImport) {
    // frame 0
    std::string image_path0 = FileFinder::FitsImagePath("noise_10px_10px.fits");
    std::shared_ptr<carta::FileLoader> loader0(carta::FileLoader::GetLoader(image_path0));
    std::shared_ptr<Frame> frame0(new Frame(0, loader0, "0"));
    // frame 1
    std::string image_path1 = FileFinder::Hdf5ImagePath("noise_10px_10px.hdf5");
    std::shared_ptr<carta::FileLoader> loader1(carta::FileLoader::GetLoader(image_path1));
    std::shared_ptr<Frame> frame1(new Frame(0, loader1, "0"));

    // Set all region types in frame0
    carta::RegionHandler region_handler;
    int file_id(0);
    int num_regions = SetAllRegions(region_handler, file_id, frame0->CoordinateSystem());
    // All CARTA regions except ANNULUS (- 1) plus 2 rotbox and 2 circle (+ 4)
    ASSERT_EQ(num_regions, CARTA::RegionType_ARRAYSIZE - 1 + 4);

    // Export all regions in frame0 (reference image)
    std::map<int, CARTA::RegionStyle> region_style_map;
    for (int i = 0; i < num_regions; ++i) {
        int region_id = i + 1; // region 0 is cursor
        CARTA::RegionType region_type = region_handler.GetRegion(region_id)->GetRegionState().type;
        CARTA::RegionStyle style = GetRegionStyle(region_type);
        region_style_map[region_id] = style;
    }
    std::string filename; // do not export to file
    bool overwrite(false);
    CARTA::ExportRegionAck export_ack0;
    region_handler.ExportRegion(file_id, frame0, CARTA::DS9_REG, CARTA::PIXEL, region_style_map, filename, overwrite, export_ack0);
    // Check that all regions were exported
    ASSERT_EQ(export_ack0.contents_size(), num_regions + 3); // header + globals, coord sys, textbox for text

    // Import all regions in frame0 (reference image)
    std::vector<std::string> export_contents = {export_ack0.contents().begin(), export_ack0.contents().end()};
    auto contents_string = ConcatContents(export_contents);
    bool file_is_filename(false);
    CARTA::ImportRegionAck import_ack0;
    region_handler.ImportRegion(file_id, frame0, CARTA::DS9_REG, contents_string, file_is_filename, import_ack0);
    // Check that all regions were imported
    ASSERT_EQ(import_ack0.regions_size(), num_regions);

    // Export all regions in frame1 (matched image)
    file_id = 1;
    CARTA::ExportRegionAck export_ack1;
    region_handler.ExportRegion(file_id, frame1, CARTA::DS9_REG, CARTA::PIXEL, region_style_map, filename, overwrite, export_ack1);
    // Check that all regions were exported
    ASSERT_EQ(export_ack1.contents_size(), num_regions + 2); // header + globals, coord sys (textbox + text in same string)

    // Import all regions in frame1 (matched image)
    export_contents = {export_ack1.contents().begin(), export_ack1.contents().end()};
    contents_string = ConcatContents(export_contents);
    CARTA::ImportRegionAck import_ack1;
    region_handler.ImportRegion(file_id, frame1, CARTA::DS9_REG, contents_string, file_is_filename, import_ack1);
    // Check that all regions were imported
    ASSERT_EQ(import_ack1.regions_size(), num_regions);
}

TEST_F(RegionImportExportTest, TestDs9WorldExportImport) {
    // frame 0
    std::string image_path0 = FileFinder::FitsImagePath("noise_10px_10px.fits");
    std::shared_ptr<carta::FileLoader> loader0(carta::FileLoader::GetLoader(image_path0));
    std::shared_ptr<Frame> frame0(new Frame(0, loader0, "0"));
    // frame 1
    std::string image_path1 = FileFinder::Hdf5ImagePath("noise_10px_10px.hdf5");
    std::shared_ptr<carta::FileLoader> loader1(carta::FileLoader::GetLoader(image_path1));
    std::shared_ptr<Frame> frame1(new Frame(0, loader1, "0"));

    // Set all region types in frame0
    carta::RegionHandler region_handler;
    int file_id(0);
    int num_regions = SetAllRegions(region_handler, file_id, frame0->CoordinateSystem());
    // All CARTA regions except ANNULUS (- 1) plus 2 rotbox and 2 circle (+ 4)
    ASSERT_EQ(num_regions, CARTA::RegionType_ARRAYSIZE - 1 + 4);

    // Export all regions in frame0 (reference image)
    std::map<int, CARTA::RegionStyle> region_style_map;
    for (int i = 0; i < num_regions; ++i) {
        int region_id = i + 1; // region 0 is cursor
        CARTA::RegionType region_type = region_handler.GetRegion(region_id)->GetRegionState().type;
        CARTA::RegionStyle style = GetRegionStyle(region_type);
        region_style_map[region_id] = style;
    }
    std::string filename; // do not export to file
    bool overwrite(false);
    CARTA::ExportRegionAck export_ack0;
    region_handler.ExportRegion(file_id, frame0, CARTA::DS9_REG, CARTA::WORLD, region_style_map, filename, overwrite, export_ack0);
    // Check that all regions were exported
    ASSERT_EQ(export_ack0.contents_size(), num_regions + 2); // header + globals, coord sys (textbox + text in same string)

    // Import all regions in frame0 (reference image)
    std::vector<std::string> export_contents = {export_ack0.contents().begin(), export_ack0.contents().end()};
    auto contents_string = ConcatContents(export_contents);
    bool file_is_filename(false);
    CARTA::ImportRegionAck import_ack0;
    region_handler.ImportRegion(file_id, frame0, CARTA::DS9_REG, contents_string, file_is_filename, import_ack0);
    // Check that all regions were imported
    ASSERT_EQ(import_ack0.regions_size(), num_regions);

    // Export all regions in frame1 (matched image)
    file_id = 1;
    CARTA::ExportRegionAck export_ack1;
    region_handler.ExportRegion(file_id, frame1, CARTA::DS9_REG, CARTA::WORLD, region_style_map, filename, overwrite, export_ack1);
    // Check that all regions were exported
    ASSERT_EQ(export_ack1.contents_size(), num_regions + 2); // header + globals, coord sys (textbox + text in same string)

    // Import all regions in frame1 (matched image)
    export_contents = {export_ack1.contents().begin(), export_ack1.contents().end()};
    contents_string = ConcatContents(export_contents);
    CARTA::ImportRegionAck import_ack1;
    region_handler.ImportRegion(file_id, frame1, CARTA::DS9_REG, contents_string, file_is_filename, import_ack1);
    // Check that all regions were imported
    ASSERT_EQ(import_ack1.regions_size(), num_regions);
}
