/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <casacore/coordinates/Coordinates/CoordinateUtil.h>
#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include <carta-protobuf/enums.pb.h>

#include "CommonTestUtilities.h"
#include "ImageData/FileLoader.h"
#include "Region/Region.h"
#include "Region/RegionHandler.h"
#include "Region/RegionImportExport/CrtfExporter.h"
#include "Region/RegionImportExport/CrtfImporter.h"
#include "Region/RegionImportExport/Ds9Exporter.h"
#include "Region/RegionImportExport/Ds9Importer.h"
#include "src/Frame/Frame.h"

using namespace carta;

TEST(AnnulusExportTest, WorldCircleWithRectangularPixels) {
    auto csys = std::make_shared<casacore::CoordinateSystem>(casacore::CoordinateUtil::defaultCoords2D());
    auto increments = csys->increment();
    increments(0) *= 2;
    ASSERT_TRUE(csys->setIncrement(increments));
    const casacore::IPosition shape(2, 20, 20);
    CrtfImporter source(csys, 0, "annulus[[0deg,0deg], [120arcsec,360arcsec]] coord=J2000", false);
    std::string error;
    auto original = source.GetRegions(error);
    ASSERT_EQ(original.size(), 1);
    auto region = std::make_shared<Region>(original[0].state, csys);
    CrtfExporter crtf(csys, shape, -1);
    RegionExporter& exporter = crtf;
    CARTA::RegionStyle style;
    style.set_color("green");
    ASSERT_TRUE(exporter.AddRegion(0, region, style, false, &error)) << error;
    CARTA::ExportRegionAck ack;
    exporter.ExportRegions("", error, ack);
    ASSERT_TRUE(ack.success());
    std::string contents;
    for (const auto& line : ack.contents())
        contents += line;
    CrtfImporter imported(csys, 0, contents, false);
    auto roundtrip = imported.GetRegions(error);
    ASSERT_EQ(roundtrip.size(), 1) << error;
    for (int axis = 1; axis <= 2; ++axis) {
        EXPECT_NEAR(roundtrip[0].state.control_points[axis].x(), original[0].state.control_points[axis].x(), 1e-4);
        EXPECT_NEAR(roundtrip[0].state.control_points[axis].y(), original[0].state.control_points[axis].y(), 1e-4);
    }
    Ds9Exporter ds9(csys, shape, false);
    RegionExporter& ds9_exporter = ds9;
    ASSERT_TRUE(ds9_exporter.AddRegion(0, region, CARTA::RegionStyle(), false));
    ds9_exporter.ExportRegions("", error, ack);
    contents.clear();
    for (const auto& line : ack.contents())
        contents += line;
    EXPECT_THAT(contents, testing::HasSubstr("120\", 360\""));
}

TEST(AnnulusExportTest, MatchedPixelRoundtripPreservesCenter) {
    auto csys = std::make_shared<casacore::CoordinateSystem>(casacore::CoordinateUtil::defaultCoords2D());
    const casacore::IPosition shape(2, 20, 20);
    for (float center : {10.0f, 100.0f}) {
        for (auto axes : {Message::Point(3, 4), Message::Point(4, 3), Message::Point(4, 4)}) {
            auto region = std::make_shared<Region>(
                RegionState(0, CARTA::ANNULUS, {Message::Point(center, center), axes, Message::Point(axes.x() / 2, axes.y() / 2)}, 15),
                csys);
            Ds9Exporter ds9(csys, shape, true);
            RegionExporter& exporter = ds9;
            ASSERT_TRUE(exporter.AddRegion(1, region, CARTA::RegionStyle(), true));
            std::string error, contents;
            CARTA::ExportRegionAck ack;
            exporter.ExportRegions("", error, ack);
            ASSERT_TRUE(ack.success());
            for (const auto& line : ack.contents())
                contents += line;
            Ds9Importer imported(csys, 1, contents, false);
            auto roundtrip = imported.GetRegions(error);
            ASSERT_EQ(roundtrip.size(), 1) << error;
            EXPECT_NEAR(roundtrip[0].state.control_points[0].x(), center, 1e-4);
            EXPECT_NEAR(roundtrip[0].state.control_points[0].y(), center, 1e-4);
        }
    }
}

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

    static RegionState GetRegionState(int file_id, CARTA::RegionInfo& region_info) {
        auto region_type = region_info.region_type();
        std::vector<CARTA::Point> control_points = {region_info.control_points().begin(), region_info.control_points().end()};
        auto rotation = region_info.rotation();
        return RegionState(file_id, region_type, control_points, rotation);
    }

    static bool RegionsEqual(const RegionState& rs0, const RegionState& rs1, CARTA::FileType region_file_type = CARTA::CRTF) {
        if (rs0.reference_file_id != rs1.reference_file_id) {
            return false;
        }
        if (rs0.type != rs1.type) {
            return false;
        }
        if (rs0.control_points.size() != rs1.control_points.size()) {
            return false;
        }
        if (std::fabs(rs0.rotation - rs1.rotation) > 1e-5) {
            return false;
        }

        for (size_t i = 0; i < rs0.control_points.size(); ++i) {
            float tolerance(1e-5);
            if (region_file_type == CARTA::DS9_REG && rs0.type == CARTA::ANNVECTOR && i == 1) {
                // DS9 vector control point is endpoint converted to length (export) converted to endpoint (import)
                tolerance = 1e-2;
            }
            if (std::fabs(rs0.control_points[i].x() - rs1.control_points[i].x()) > tolerance) {
                return false;
            }
            if (std::fabs(rs0.control_points[i].y() - rs1.control_points[i].y()) > tolerance) {
                return false;
            }
        }
        return true;
    }
};

TEST_F(RegionImportExportTest, TestCrtfPixExportImport) {
    // frame 0
    auto image_path0 = FitsImages() / "noise_10px_10px.fits";
    auto loader0 = carta::FileLoader::GetLoader(image_path0);
    std::shared_ptr<Frame> frame0(new Frame(0, loader0, "0"));
    // frame 1
    auto image_path1 = Hdf5Images() / "noise_10px_10px.hdf5";
    auto loader1 = carta::FileLoader::GetLoader(image_path1);
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
    // Check that all regions were imported correctly and equal original regions
    ASSERT_EQ(import_ack0.regions_size(), num_regions);
    std::map<int, CARTA::RegionInfo> imported_regions = {import_ack0.regions().begin(), import_ack0.regions().end()};
    for (auto imported_region : imported_regions) {
        auto imported_region_id = imported_region.first;
        auto imported_region_state = GetRegionState(file_id, imported_region.second);
        auto original_region_state = region_handler.GetRegion(imported_region_id - num_regions)->GetRegionState();
        ASSERT_TRUE(RegionsEqual(imported_region_state, original_region_state));
    }

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
    auto image_path0 = FitsImages() / "noise_10px_10px.fits";
    auto loader0 = carta::FileLoader::GetLoader(image_path0);
    std::shared_ptr<Frame> frame0(new Frame(0, loader0, "0"));
    // frame 1
    auto image_path1 = Hdf5Images() / "noise_10px_10px.hdf5";
    auto loader1 = carta::FileLoader::GetLoader(image_path1);
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
    // Check that all regions were imported correctly and equal original regions
    ASSERT_EQ(import_ack0.regions_size(), num_regions);
    std::map<int, CARTA::RegionInfo> imported_regions = {import_ack0.regions().begin(), import_ack0.regions().end()};
    for (auto imported_region : imported_regions) {
        auto imported_region_id = imported_region.first;
        auto imported_region_state = GetRegionState(file_id, imported_region.second);
        auto original_region_state = region_handler.GetRegion(imported_region_id - num_regions)->GetRegionState();
        ASSERT_TRUE(RegionsEqual(imported_region_state, original_region_state));
    }

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
    auto image_path0 = FitsImages() / "noise_10px_10px.fits";
    auto loader0 = carta::FileLoader::GetLoader(image_path0);
    std::shared_ptr<Frame> frame0(new Frame(0, loader0, "0"));
    // frame 1
    auto image_path1 = Hdf5Images() / "noise_10px_10px.hdf5";
    auto loader1 = carta::FileLoader::GetLoader(image_path1);
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
    // Check that all regions were imported correctly and equal original regions
    ASSERT_EQ(import_ack0.regions_size(), num_regions);
    std::map<int, CARTA::RegionInfo> imported_regions = {import_ack0.regions().begin(), import_ack0.regions().end()};
    for (auto imported_region : imported_regions) {
        auto imported_region_id = imported_region.first;
        auto imported_region_state = GetRegionState(file_id, imported_region.second);
        auto original_region_state = region_handler.GetRegion(imported_region_id - num_regions)->GetRegionState();
        ASSERT_TRUE(RegionsEqual(imported_region_state, original_region_state, CARTA::DS9_REG));
    }

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
    auto image_path0 = FitsImages() / "noise_10px_10px.fits";
    auto loader0 = carta::FileLoader::GetLoader(image_path0);
    std::shared_ptr<Frame> frame0(new Frame(0, loader0, "0"));
    // frame 1
    auto image_path1 = Hdf5Images() / "noise_10px_10px.hdf5";
    auto loader1 = carta::FileLoader::GetLoader(image_path1);
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
    // Check that all regions were imported correctly and equal original regions
    ASSERT_EQ(import_ack0.regions_size(), num_regions);
    std::map<int, CARTA::RegionInfo> imported_regions = {import_ack0.regions().begin(), import_ack0.regions().end()};
    for (auto imported_region : imported_regions) {
        auto imported_region_id = imported_region.first;
        auto imported_region_state = GetRegionState(file_id, imported_region.second);
        auto original_region_state = region_handler.GetRegion(imported_region_id - num_regions)->GetRegionState();
        ASSERT_TRUE(RegionsEqual(imported_region_state, original_region_state, CARTA::DS9_REG));
    }

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

TEST_F(RegionImportExportTest, TestDs9AnnulusExportImport) {
    auto image_path0 = FitsImages() / "noise_10px_10px.fits";
    auto loader0 = carta::FileLoader::GetLoader(image_path0);
    std::shared_ptr<Frame> frame0(new Frame(0, loader0, "0"));

    carta::RegionHandler region_handler;
    int file_id(0);
    int region_id(-1);
    std::vector<float> annulus_points = {5.0, 5.0, 3.0, 4.0, 1.5, 2.0};
    float rotation(15.0);
    ASSERT_TRUE(SetRegion(region_handler, file_id, region_id, CARTA::ANNULUS, annulus_points, rotation, frame0->CoordinateSystem()));

    std::map<int, CARTA::RegionStyle> region_style_map;
    region_style_map[region_id] = GetRegionStyle(CARTA::ANNULUS);

    std::string filename;
    bool overwrite(false);
    CARTA::ExportRegionAck export_ack;
    region_handler.ExportRegion(file_id, frame0, CARTA::DS9_REG, CARTA::PIXEL, region_style_map, filename, overwrite, export_ack);
    ASSERT_GT(export_ack.contents_size(), 0);

    std::vector<std::string> export_contents = {export_ack.contents().begin(), export_ack.contents().end()};
    auto contents_string = ConcatContents(export_contents);
    bool file_is_filename(false);
    CARTA::ImportRegionAck import_ack;
    region_handler.ImportRegion(file_id, frame0, CARTA::DS9_REG, contents_string, file_is_filename, import_ack);
    ASSERT_EQ(import_ack.regions_size(), 1);

    CARTA::RegionInfo imported_info = import_ack.regions().begin()->second;
    auto imported_region_state = GetRegionState(file_id, imported_info);
    auto original_region_state = region_handler.GetRegion(region_id)->GetRegionState();
    ASSERT_TRUE(RegionsEqual(imported_region_state, original_region_state, CARTA::DS9_REG));

    CARTA::ExportRegionAck world_export_ack;
    region_handler.ExportRegion(file_id, frame0, CARTA::DS9_REG, CARTA::WORLD, region_style_map, filename, overwrite, world_export_ack);
    ASSERT_GT(world_export_ack.contents_size(), 0);

    export_contents = {world_export_ack.contents().begin(), world_export_ack.contents().end()};
    contents_string = ConcatContents(export_contents);
    CARTA::ImportRegionAck world_import_ack;
    region_handler.ImportRegion(file_id, frame0, CARTA::DS9_REG, contents_string, file_is_filename, world_import_ack);
    ASSERT_EQ(world_import_ack.regions_size(), 1);

    imported_info = world_import_ack.regions().begin()->second;
    imported_region_state = GetRegionState(file_id, imported_info);
    ASSERT_TRUE(RegionsEqual(imported_region_state, original_region_state, CARTA::DS9_REG));

    CARTA::ExportRegionAck crtf_export_ack;
    region_handler.ExportRegion(file_id, frame0, CARTA::CRTF, CARTA::WORLD, region_style_map, filename, overwrite, crtf_export_ack);
    ASSERT_FALSE(crtf_export_ack.success());
    EXPECT_THAT(
        crtf_export_ack.message(), testing::HasSubstr("Region 1: Ellipse annulus is not supported in CRTF. Please save as DS9 format."));
}

TEST_F(RegionImportExportTest, TestDs9AnnotationAnnulusRejectedWithMessage) {
    auto image_path = FitsImages() / "noise_10px_10px.fits";
    auto loader = carta::FileLoader::GetLoader(image_path);
    std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));
    carta::RegionHandler region_handler;
    const std::string contents = "image\n# ellipse(5,5,3,4,2,2,0)\n";
    CARTA::ImportRegionAck import_ack;

    region_handler.ImportRegion(0, frame, CARTA::DS9_REG, contents, false, import_ack);

    EXPECT_FALSE(import_ack.success());
    EXPECT_EQ(import_ack.regions_size(), 0);
    EXPECT_THAT(import_ack.message(), testing::HasSubstr("Annotation annulus is not supported"));
}

TEST_F(RegionImportExportTest, TestDs9AnnulusAxisRoundingIsNormalized) {
    auto image_path = FitsImages() / "noise_10px_10px.fits";
    auto loader = carta::FileLoader::GetLoader(image_path);
    std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));
    carta::RegionHandler region_handler;
    const std::string contents = "image\nellipse(5,5,10,20,5.01,10,0)\n";
    CARTA::ImportRegionAck import_ack;

    region_handler.ImportRegion(0, frame, CARTA::DS9_REG, contents, false, import_ack);

    ASSERT_EQ(import_ack.regions_size(), 1) << import_ack.message();
    auto region_info = import_ack.regions().begin()->second;
    const auto region_state = GetRegionState(0, region_info);
    ASSERT_EQ(region_state.control_points.size(), 3);
    EXPECT_NEAR(region_state.control_points[2].x() / region_state.control_points[2].y(), 0.5, 1e-5);
    EXPECT_NEAR(region_state.control_points[2].x() * region_state.control_points[2].y(), 5.01 * 10.0, 1e-4);
}

TEST_F(RegionImportExportTest, TestCrtfCircularAnnulusExport) {
    auto image_path = FitsImages() / "noise_10px_10px.fits";
    auto loader = carta::FileLoader::GetLoader(image_path);
    std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));
    carta::RegionHandler region_handler;
    int region_id(-1);
    std::vector<float> annulus_points = {5.0, 5.0, 3.0, 3.0, 1.0, 1.0};
    ASSERT_TRUE(SetRegion(region_handler, 0, region_id, CARTA::ANNULUS, annulus_points, 0.0, frame->CoordinateSystem()));
    std::map<int, CARTA::RegionStyle> region_styles{{region_id, GetRegionStyle(CARTA::ANNULUS)}};
    std::string filename;
    bool overwrite(false);

    CARTA::ExportRegionAck pixel_export_ack;
    region_handler.ExportRegion(0, frame, CARTA::CRTF, CARTA::PIXEL, region_styles, filename, overwrite, pixel_export_ack);
    ASSERT_TRUE(pixel_export_ack.success()) << pixel_export_ack.message();
    EXPECT_THAT(pixel_export_ack.contents().Get(1), testing::HasSubstr("annulus[[5.0000pix, 5.0000pix], [1.0000pix, 3.0000pix]]"));

    std::vector<std::string> export_contents{pixel_export_ack.contents().begin(), pixel_export_ack.contents().end()};
    const auto contents_string = ConcatContents(export_contents);
    CARTA::ImportRegionAck import_ack;
    region_handler.ImportRegion(0, frame, CARTA::CRTF, contents_string, false, import_ack);
    ASSERT_EQ(import_ack.regions_size(), 1) << import_ack.message();
    auto imported_info = import_ack.regions().begin()->second;
    const auto imported_region_state = GetRegionState(0, imported_info);
    const auto original_region_state = region_handler.GetRegion(region_id)->GetRegionState();
    EXPECT_TRUE(RegionsEqual(imported_region_state, original_region_state));

    CARTA::ExportRegionAck world_export_ack;
    region_handler.ExportRegion(0, frame, CARTA::CRTF, CARTA::WORLD, region_styles, filename, overwrite, world_export_ack);
    ASSERT_TRUE(world_export_ack.success()) << world_export_ack.message();
    EXPECT_THAT(world_export_ack.contents().Get(1), testing::HasSubstr("annulus"));

    int elliptical_id(-1);
    ASSERT_TRUE(SetRegion(region_handler, 0, elliptical_id, CARTA::ANNULUS, {5, 5, 3, 4, 1.5, 2}, 0, frame->CoordinateSystem()));
    region_styles[elliptical_id] = GetRegionStyle(CARTA::ANNULUS);
    CARTA::ExportRegionAck partial_ack;
    region_handler.ExportRegion(0, frame, CARTA::CRTF, CARTA::PIXEL, region_styles, filename, overwrite, partial_ack);
    EXPECT_TRUE(partial_ack.success());
    EXPECT_EQ(partial_ack.contents_size(), pixel_export_ack.contents_size());
    EXPECT_THAT(partial_ack.message(), testing::HasSubstr("Region 3: Ellipse annulus is not supported in CRTF."));
}

TEST_F(RegionImportExportTest, TestInvalidAnnulusAxesRejected) {
    auto image_path = FitsImages() / "noise_10px_10px.fits";
    auto loader = carta::FileLoader::GetLoader(image_path);
    std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));
    carta::RegionHandler region_handler;
    int region_id(-1);
    std::vector<float> invalid_annulus_points = {5.0, 5.0, 3.0, 4.0, 3.5, 2.0};
    EXPECT_FALSE(SetRegion(region_handler, 0, region_id, CARTA::ANNULUS, invalid_annulus_points, 0.0, frame->CoordinateSystem()));

    region_id = -1;
    std::vector<float> different_shape_points = {5.0, 5.0, 10.0, 20.0, 5.0, 5.0};
    EXPECT_FALSE(SetRegion(region_handler, 0, region_id, CARTA::ANNULUS, different_shape_points, 0.0, frame->CoordinateSystem()));
}
