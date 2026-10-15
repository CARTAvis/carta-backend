/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <casacore/coordinates/Coordinates/CoordinateUtil.h>
#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>
#include <cmath>
#include <set>

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

namespace {
std::set<std::pair<int, int>> SelectedPixels(
    Region& region, int file_id, std::shared_ptr<casacore::CoordinateSystem> csys, const casacore::IPosition& shape) {
    std::set<std::pair<int, int>> pixels;
    auto lc = region.GetLCRegion(file_id, csys, shape);
    if (!lc) {
        return pixels;
    }
    auto mask = region.GetImageRegionMask(file_id);
    const auto start = lc->boundingBox().start();
    for (int y = 0; y < mask.shape()(1); ++y) {
        for (int x = 0; x < mask.shape()(0); ++x) {
            if (mask.getAt(casacore::IPosition(2, x, y))) {
                pixels.emplace(x + start(0), y + start(1));
            }
        }
    }
    return pixels;
}
} // namespace

TEST(AnnulusExportTest, Ds9EllipticalAnnulusPreservesAxisOrderAndPositionAngle) {
    const casacore::IPosition shape(2, 100, 100);
    for (bool near_square : {false, true}) {
        auto csys = std::make_shared<casacore::CoordinateSystem>(casacore::CoordinateUtil::defaultCoords2D());
        auto increments = csys->increment();
        increments(0) *= near_square ? 1.0005 : 1;
        ASSERT_TRUE(csys->setIncrement(increments));
        auto reference = csys->referencePixel();
        reference(0) = reference(1) = 50;
        ASSERT_TRUE(csys->setReferencePixel(reference));
        for (bool swapped : {false, true}) {
            auto outer = Message::Point(swapped ? 10 : 5, swapped ? 5 : 10);
            auto inner = Message::Point(outer.x() / 2, outer.y() / 2);
            for (float pa : {0.f, 30.f, 90.f, 120.f, 210.f, 270.f, 300.f}) {
                auto region = std::make_shared<Region>(RegionState(0, CARTA::ANNULUS, {Message::Point(50, 50), outer, inner}, pa), csys);
                for (bool pixels : {false, true}) {
                    SCOPED_TRACE(fmt::format("near_square={}, swapped={}, pa={}, pixels={}", near_square, swapped, pa, pixels));
                    for (int file_id : {0, 1}) {
                        SCOPED_TRACE(file_id);
                        Ds9Exporter ds9(csys, shape, pixels);
                        RegionExporter& exporter = ds9;
                        std::string error, contents;
                        ASSERT_TRUE(exporter.AddRegion(file_id, region, CARTA::RegionStyle(), pixels, &error)) << error;
                        CARTA::ExportRegionAck ack;
                        exporter.ExportRegions("", error, ack);
                        ASSERT_TRUE(ack.success()) << error;
                        for (const auto& line : ack.contents()) {
                            contents += line;
                        }
                        Ds9Importer importer(csys, file_id, contents, false);
                        auto imported = importer.GetRegions(error);
                        ASSERT_EQ(imported.size(), 1) << error;
                        EXPECT_NEAR(imported[0].state.rotation, pa, 1e-3);
                        for (int axis = 1; axis <= 2; ++axis) {
                            EXPECT_NEAR(
                                imported[0].state.control_points[axis].x(), region->GetRegionState().control_points[axis].x(), 1e-3);
                            EXPECT_NEAR(
                                imported[0].state.control_points[axis].y(), region->GetRegionState().control_points[axis].y(), 1e-3);
                        }
                    }
                }
            }
        }
    }
}

TEST(AnnulusExportTest, MatchedAnnulusPositionAngleFollowsDirectedAxis) {
    auto source = std::make_shared<casacore::CoordinateSystem>(casacore::CoordinateUtil::defaultCoords2D());
    auto reference = source->referencePixel();
    reference(0) = reference(1) = 20;
    ASSERT_TRUE(source->setReferencePixel(reference));
    const casacore::IPosition shape(2, 40, 40);
    for (bool mirrored : {false, true}) {
        auto target = std::make_shared<casacore::CoordinateSystem>(*source);
        casacore::Matrix<casacore::Double> transform(2, 2, 0.0);
        if (mirrored) {
            transform(0, 0) = -1;
            transform(1, 1) = 1;
        } else {
            transform(0, 1) = -1;
            transform(1, 0) = 1;
        }
        ASSERT_TRUE(target->setLinearTransform(transform));
        // Derive the directed PA independently through the coordinate systems, including RA-axis handedness.
        casacore::Vector<casacore::Double> axis_endpoint(reference.copy());
        axis_endpoint(0) -= std::sin(210 * M_PI / 180.0);
        axis_endpoint(1) += std::cos(210 * M_PI / 180.0);
        const auto target_endpoint = target->toPixel(source->toWorld(axis_endpoint));
        const double expected_pa = std::fmod(std::atan2(-(target_endpoint(0) - 20), target_endpoint(1) - 20) * 180.0 / M_PI + 360, 360);
        for (bool swapped : {false, true}) {
            const auto outer = Message::Point(swapped ? 7 : 3, swapped ? 3 : 7);
            const auto inner = Message::Point(outer.x() / 2, outer.y() / 2);
            for (auto type : {CARTA::ANNULUS, CARTA::ANNANNULUS}) {
                auto region = std::make_shared<Region>(RegionState(0, type, {Message::Point(20, 20), outer, inner}, 210), source);
                for (bool pixels : {false, true}) {
                    SCOPED_TRACE(fmt::format("mirrored={}, swapped={}, type={}, pixels={}", mirrored, swapped, type, pixels));
                    Ds9Exporter ds9(target, shape, pixels);
                    RegionExporter& exporter = ds9;
                    CARTA::RegionStyle style;
                    style.set_color("green");
                    std::string error, contents;
                    ASSERT_TRUE(exporter.AddRegion(1, region, style, pixels, &error)) << error;
                    CARTA::ExportRegionAck ack;
                    exporter.ExportRegions("", error, ack);
                    ASSERT_TRUE(ack.success()) << error;
                    for (const auto& line : ack.contents()) {
                        contents += line;
                    }
                    Ds9Importer importer(target, 1, contents, false);
                    auto imported = importer.GetRegions(error);
                    ASSERT_EQ(imported.size(), 1) << error << contents;
                    EXPECT_EQ(imported[0].state.type, type);
                    EXPECT_NEAR(imported[0].state.rotation, expected_pa, 1e-3);
                    EXPECT_NEAR(imported[0].state.control_points[1].x(), outer.x(), 1e-3);
                    EXPECT_NEAR(imported[0].state.control_points[1].y(), outer.y(), 1e-3);
                }
            }
        }
    }
}

TEST(AnnulusExportTest, OrdinaryEllipseWorldExportRetainsExistingAxisConvention) {
    auto csys = std::make_shared<casacore::CoordinateSystem>(casacore::CoordinateUtil::defaultCoords2D());
    auto increments = csys->increment();
    increments(0) *= 2;
    ASSERT_TRUE(csys->setIncrement(increments));
    auto reference = csys->referencePixel();
    reference(0) = reference(1) = 20;
    ASSERT_TRUE(csys->setReferencePixel(reference));
    const casacore::IPosition shape(2, 40, 40);
    for (auto type : {CARTA::ELLIPSE, CARTA::ANNELLIPSE}) {
        auto region = std::make_shared<Region>(RegionState(0, type, {Message::Point(20, 20), Message::Point(6, 3)}, 0), csys);
        for (bool crtf : {false, true}) {
            SCOPED_TRACE(fmt::format("type={}, crtf={}", type, crtf));
            Ds9Exporter ds9(csys, shape, false);
            CrtfExporter crtf_exporter(csys, shape, -1);
            RegionExporter& exporter = crtf ? static_cast<RegionExporter&>(crtf_exporter) : static_cast<RegionExporter&>(ds9);
            CARTA::RegionStyle style;
            style.set_color("green");
            std::string error, contents;
            ASSERT_TRUE(exporter.AddRegion(0, region, style, false, &error)) << error;
            CARTA::ExportRegionAck ack;
            exporter.ExportRegions("", error, ack);
            ASSERT_TRUE(ack.success()) << error;
            for (const auto& line : ack.contents()) {
                contents += line;
            }
            Ds9Importer ds9_importer(csys, 0, crtf ? "" : contents, false);
            CrtfImporter crtf_importer(csys, 0, crtf ? contents : "", false);
            RegionImporter& importer = crtf ? static_cast<RegionImporter&>(crtf_importer) : static_cast<RegionImporter&>(ds9_importer);
            auto imported = importer.GetRegions(error);
            ASSERT_EQ(imported.size(), 1) << error << contents;
            EXPECT_EQ(imported[0].state.type, type);
            EXPECT_NEAR(imported[0].state.control_points[1].x(), 6, 1e-3);
            EXPECT_NEAR(imported[0].state.control_points[1].y(), 3, 1e-3);
            EXPECT_NEAR(imported[0].state.rotation, 0, 1e-3);
        }
    }
}

TEST(AnnulusExportTest, SharedWcsTransformMatchesIndependentRingConversions) {
    const auto csys = casacore::CoordinateUtil::defaultCoords2D();
    casacore::Vector<casacore::Double> center(2, 0.0);
    center(0) = 20;
    center(1) = 30;
    for (bool preserve_order : {false, true}) {
        for (const std::string frame : {"", "GALACTIC"}) {
            SCOPED_TRACE(fmt::format("preserve_order={}, frame={}", preserve_order, frame));
            EllipseWcsTransform shared_forward, shared_inverse;
            EXPECT_FALSE(shared_forward.initialized);
            EXPECT_FALSE(shared_inverse.initialized);
            for (double ratio : {1.0, 0.5}) {
                casacore::Quantity actual_angle(210, "deg"), expected_angle(210, "deg");
                std::vector<casacore::Quantity> actual_axes, expected_axes;
                ASSERT_TRUE(PixelEllipseAxesToWorld(
                    csys, 3 * ratio, 7 * ratio, actual_angle, actual_axes, &center, preserve_order, &shared_forward));
                ASSERT_TRUE(PixelEllipseAxesToWorld(csys, 3 * ratio, 7 * ratio, expected_angle, expected_axes, &center, preserve_order));
                EXPECT_TRUE(shared_forward.initialized);
                EXPECT_DOUBLE_EQ(actual_axes[0].get("arcsec").getValue(), expected_axes[0].get("arcsec").getValue());
                EXPECT_DOUBLE_EQ(actual_axes[1].get("arcsec").getValue(), expected_axes[1].get("arcsec").getValue());
                EXPECT_DOUBLE_EQ(actual_angle.get("deg").getValue(), expected_angle.get("deg").getValue());

                // The same transform must also work when the inner ring uses different angular units.
                actual_axes[0].convert(ratio == 1.0 ? "arcsec" : "deg");
                CARTA::Point actual_pixel_axes, expected_pixel_axes;
                double actual_pa, expected_pa;
                ASSERT_TRUE(WorldEllipseAxesToPixels(csys, center, actual_axes[0], actual_axes[1], actual_angle.get("deg").getValue(),
                    frame, actual_pixel_axes, actual_pa, preserve_order, &shared_inverse));
                ASSERT_TRUE(WorldEllipseAxesToPixels(csys, center, actual_axes[0], actual_axes[1], actual_angle.get("deg").getValue(),
                    frame, expected_pixel_axes, expected_pa, preserve_order));
                EXPECT_TRUE(shared_inverse.initialized);
                EXPECT_FLOAT_EQ(actual_pixel_axes.x(), expected_pixel_axes.x());
                EXPECT_FLOAT_EQ(actual_pixel_axes.y(), expected_pixel_axes.y());
                EXPECT_DOUBLE_EQ(actual_pa, expected_pa);
            }
        }
    }
}

TEST(AnnulusExportTest, SharedWcsTransformCanRetryFailedInitialization) {
    const auto csys = casacore::CoordinateUtil::defaultCoords2D();
    casacore::Vector<casacore::Double> center(2, 0.0);
    EllipseWcsTransform shared_transform;
    CARTA::Point axes;
    double rotation;
    const casacore::Quantity radius(1, "arcsec");
    EXPECT_FALSE(WorldEllipseAxesToPixels(csys, center, radius, radius, 0, "invalid-frame", axes, rotation, false, &shared_transform));
    EXPECT_FALSE(shared_transform.initialized);
    EXPECT_TRUE(WorldEllipseAxesToPixels(csys, center, radius, radius, 0, "", axes, rotation, false, &shared_transform));
    EXPECT_TRUE(shared_transform.initialized);
}

TEST(AnnulusExportTest, AnnotationAnnulusRoundtrip) {
    auto csys = std::make_shared<casacore::CoordinateSystem>(casacore::CoordinateUtil::defaultCoords2D());
    auto reference = csys->referencePixel();
    reference(0) = reference(1) = 20;
    ASSERT_TRUE(csys->setReferencePixel(reference));
    const casacore::IPosition shape(2, 40, 40);
    for (bool circular : {false, true}) {
        const auto outer = Message::Point(3, circular ? 3 : 6);
        const auto inner = Message::Point(1.5, circular ? 1.5 : 3);
        auto region =
            std::make_shared<Region>(RegionState(0, CARTA::ANNANNULUS, {Message::Point(20, 20), outer, inner}, circular ? 0 : 210), csys);
        ASSERT_TRUE(region->GetRegionState().IsAnnotation());
        for (bool pixels : {false, true}) {
            for (bool crtf : {false, true}) {
                if (crtf && !circular) {
                    continue;
                }
                SCOPED_TRACE(fmt::format("circular={}, pixels={}, crtf={}", circular, pixels, crtf));
                Ds9Exporter ds9(csys, shape, pixels);
                CrtfExporter crtf_exporter(csys, shape, -1);
                RegionExporter& exporter = crtf ? static_cast<RegionExporter&>(crtf_exporter) : static_cast<RegionExporter&>(ds9);
                CARTA::RegionStyle style;
                style.set_color("green");
                std::string error, contents;
                ASSERT_TRUE(exporter.AddRegion(0, region, style, pixels, &error)) << error;
                CARTA::ExportRegionAck ack;
                exporter.ExportRegions("", error, ack);
                ASSERT_TRUE(ack.success()) << error;
                for (const auto& line : ack.contents()) {
                    contents += line;
                }
                Ds9Importer ds9_importer(csys, 0, crtf ? "" : contents, false);
                CrtfImporter crtf_importer(csys, 0, crtf ? contents : "", false);
                RegionImporter& importer = crtf ? static_cast<RegionImporter&>(crtf_importer) : static_cast<RegionImporter&>(ds9_importer);
                auto imported = importer.GetRegions(error);
                ASSERT_EQ(imported.size(), 1) << error << contents;
                EXPECT_EQ(imported[0].state.type, CARTA::ANNANNULUS) << contents;
                EXPECT_NEAR(imported[0].state.rotation, circular ? 0 : 210, 1e-3);
                EXPECT_NEAR(imported[0].state.control_points[1].x(), outer.x(), 1e-3);
                EXPECT_NEAR(imported[0].state.control_points[1].y(), outer.y(), 1e-3);
                EXPECT_NEAR(imported[0].state.control_points[2].x(), inner.x(), 1e-3);
                EXPECT_NEAR(imported[0].state.control_points[2].y(), inner.y(), 1e-3);
            }
        }
    }
}

TEST(AnnulusExportTest, CrtfAnnulusImportsWithZeroPositionAngle) {
    auto csys = std::make_shared<casacore::CoordinateSystem>(casacore::CoordinateUtil::defaultCoords2D());
    auto increments = csys->increment();
    increments(0) *= 1.0005;
    ASSERT_TRUE(csys->setIncrement(increments));
    const std::vector<std::string> contents = {
        "annulus[[20pix,20pix],[2pix,4pix]]", "annulus[[0deg,0deg],[120arcsec,360arcsec]] coord=J2000"};
    for (double angle : {0.0, 30.0, 90.0}) {
        casacore::Matrix<casacore::Double> transform(2, 2);
        const double radians = angle * M_PI / 180.0;
        transform(0, 0) = transform(1, 1) = std::cos(radians);
        transform(0, 1) = -std::sin(radians);
        transform(1, 0) = std::sin(radians);
        ASSERT_TRUE(csys->setLinearTransform(transform));
        for (const auto& text : contents) {
            CrtfImporter importer(csys, 0, text, false);
            std::string error;
            auto regions = importer.GetRegions(error);
            ASSERT_EQ(regions.size(), 1) << error;
            EXPECT_EQ(regions[0].state.type, CARTA::ANNULUS);
            EXPECT_FLOAT_EQ(regions[0].state.rotation, 0.0);
        }
    }
}

TEST(AnnulusExportTest, WorldGeometryPreservesRotationAndReflection) {
    const casacore::IPosition shape(2, 40, 40);
    for (bool rectangular : {false, true}) {
        auto source = std::make_shared<casacore::CoordinateSystem>(casacore::CoordinateUtil::defaultCoords2D());
        auto increments = source->increment();
        increments(0) *= rectangular ? 2 : 1;
        ASSERT_TRUE(source->setIncrement(increments));
        auto reference = source->referencePixel();
        reference(0) = reference(1) = 20;
        ASSERT_TRUE(source->setReferencePixel(reference));
        for (bool mirrored : {false, true}) {
            auto target = std::make_shared<casacore::CoordinateSystem>(*source);
            casacore::Matrix<casacore::Double> transform(2, 2, 0.0);
            if (mirrored) {
                transform(0, 0) = -1;
                transform(1, 1) = 1;
            } else {
                transform(0, 1) = -1;
                transform(1, 0) = 1;
            }
            ASSERT_TRUE(target->setLinearTransform(transform));
            auto region = std::make_shared<Region>(
                RegionState(0, CARTA::ANNULUS, {Message::Point(20, 20), Message::Point(3, 7), Message::Point(1.5, 3.5)}, 30), source);
            const auto expected = SelectedPixels(*region, 0, source, shape);
            ASSERT_FALSE(expected.empty());
            for (int file_id : {0, 1}) {
                Ds9Exporter ds9(file_id == 0 ? source : target, shape, false);
                RegionExporter& exporter = ds9;
                std::string error, contents;
                ASSERT_TRUE(exporter.AddRegion(file_id, region, CARTA::RegionStyle(), false, &error)) << error;
                CARTA::ExportRegionAck ack;
                exporter.ExportRegions("", error, ack);
                ASSERT_TRUE(ack.success());
                for (const auto& line : ack.contents()) {
                    contents += line;
                }
                Ds9Importer importer(source, 0, contents, false);
                auto imported = importer.GetRegions(error);
                ASSERT_EQ(imported.size(), 1) << error;
                Region restored(imported[0].state, source);
                EXPECT_EQ(SelectedPixels(restored, 0, source, shape), expected) << contents;
            }

            for (const auto& contents : {std::string("fk5\nellipse(0,0,90\",210\",180\",420\",120)"),
                     std::string("fk5\nannulus(0,0,120\",360\")"), std::string("annulus[[0deg,0deg],[120arcsec,360arcsec]] coord=J2000")}) {
                const bool crtf = contents.front() != 'f';
                Ds9Importer ds9_source(source, 0, crtf ? "" : contents, false), ds9_target(target, 1, crtf ? "" : contents, false);
                CrtfImporter crtf_source(source, 0, crtf ? contents : "", false), crtf_target(target, 1, crtf ? contents : "", false);
                RegionImporter& source_importer =
                    crtf ? static_cast<RegionImporter&>(crtf_source) : static_cast<RegionImporter&>(ds9_source);
                RegionImporter& target_importer =
                    crtf ? static_cast<RegionImporter&>(crtf_target) : static_cast<RegionImporter&>(ds9_target);
                std::string error;
                auto original = source_importer.GetRegions(error), imported = target_importer.GetRegions(error);
                ASSERT_EQ(original.size(), 1) << error;
                ASSERT_EQ(imported.size(), 1) << error;
                if (crtf) {
                    EXPECT_FLOAT_EQ(original[0].state.rotation, 0.0);
                    EXPECT_FLOAT_EQ(imported[0].state.rotation, 0.0);
                    continue;
                }
                auto reference_region = std::make_shared<Region>(original[0].state, source);
                Region target_region(imported[0].state, target);
                const auto matched = SelectedPixels(*reference_region, 1, target, shape);
                ASSERT_FALSE(matched.empty());
                EXPECT_EQ(SelectedPixels(target_region, 1, target, shape), matched) << contents;
            }
        }
    }
}

TEST(AnnulusExportTest, WorldCircleWithRectangularPixels) {
    auto csys = std::make_shared<casacore::CoordinateSystem>(casacore::CoordinateUtil::defaultCoords2D());
    auto increments = csys->increment();
    increments(0) *= 2;
    ASSERT_TRUE(csys->setIncrement(increments));
    auto reference = csys->referencePixel();
    reference(0) = reference(1) = 10;
    ASSERT_TRUE(csys->setReferencePixel(reference));
    const casacore::IPosition shape(2, 20, 20);
    Ds9Importer source(csys, 0, "fk5\nannulus(0,0,120\",360\")", false);
    std::string error;
    auto original = source.GetRegions(error);
    ASSERT_EQ(original.size(), 1);
    auto region = std::make_shared<Region>(original[0].state, csys);
    EXPECT_FLOAT_EQ(std::fmod(original[0].state.rotation, 180), 90);
    auto lc = region->GetLCRegion(0, csys, shape);
    ASSERT_TRUE(lc);
    EXPECT_EQ(lc->boundingBox().length(), casacore::IPosition(2, 7, 13));
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
    EXPECT_FLOAT_EQ(roundtrip[0].state.rotation, 0.0);
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
    Ds9Importer ds9_importer(csys, 0, contents, false);
    SCOPED_TRACE(contents);
    auto ds9_roundtrip = ds9_importer.GetRegions(error);
    ASSERT_EQ(ds9_roundtrip.size(), 1) << error;
    EXPECT_NEAR(ds9_roundtrip[0].state.control_points[1].x(), 3, 1e-4);
    EXPECT_NEAR(ds9_roundtrip[0].state.control_points[1].y(), 6, 1e-4);
    Region ds9_region(ds9_roundtrip[0].state, csys);
    auto ds9_lc = ds9_region.GetLCRegion(0, csys, shape);
    ASSERT_TRUE(ds9_lc);
    EXPECT_EQ(ds9_lc->boundingBox().length(), lc->boundingBox().length());
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

TEST(AnnulusExportTest, MatchedRotationPreservesGeometry) {
    auto source = std::make_shared<casacore::CoordinateSystem>(casacore::CoordinateUtil::defaultCoords2D());
    auto increments = source->increment();
    increments(0) *= 2;
    ASSERT_TRUE(source->setIncrement(increments));
    auto reference = source->referencePixel();
    reference(0) = reference(1) = 20;
    ASSERT_TRUE(source->setReferencePixel(reference));
    auto target = std::make_shared<casacore::CoordinateSystem>(*source);
    casacore::Matrix<casacore::Double> rotation(2, 2, 0.0);
    rotation(0, 1) = -1;
    rotation(1, 0) = 1;
    ASSERT_TRUE(target->setLinearTransform(rotation));

    const casacore::IPosition shape(2, 40, 40);
    auto region = std::make_shared<Region>(
        RegionState(0, CARTA::ANNULUS, {Message::Point(20, 20), Message::Point(3, 7), Message::Point(1.5, 3.5)}, 30), source);
    Ds9Exporter exporter(target, shape, true);
    RegionExporter& region_exporter = exporter;
    ASSERT_TRUE(region_exporter.AddRegion(1, region, CARTA::RegionStyle(), true));
    std::string error, contents;
    CARTA::ExportRegionAck ack;
    region_exporter.ExportRegions("", error, ack);
    ASSERT_TRUE(ack.success()) << error;
    for (const auto& line : ack.contents()) {
        contents += line;
    }
    Ds9Importer importer(target, 1, contents, false);
    auto imported = importer.GetRegions(error);
    ASSERT_EQ(imported.size(), 1) << error;
    SCOPED_TRACE(contents);

    auto matched = region->GetLCRegion(1, target, shape);
    Region restored(imported[0].state, target);
    auto roundtrip = restored.GetLCRegion(0, target, shape);
    ASSERT_TRUE(matched);
    ASSERT_TRUE(roundtrip);
    auto selected_pixels = [](Region& current, int file_id, const std::shared_ptr<casacore::LCRegion>& lc) {
        std::set<std::pair<int, int>> pixels;
        auto mask = current.GetImageRegionMask(file_id);
        const auto start = lc->boundingBox().start();
        for (int y = 0; y < mask.shape()(1); ++y) {
            for (int x = 0; x < mask.shape()(0); ++x) {
                if (mask.getAt(casacore::IPosition(2, x, y))) {
                    pixels.emplace(x + start(0), y + start(1));
                }
            }
        }
        return pixels;
    };
    EXPECT_EQ(selected_pixels(*region, 1, matched), selected_pixels(restored, 0, roundtrip)) << contents;
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
            {CARTA::ANNCOMPASS, circle_points}, {CARTA::ANNANNULUS, {5.0, 5.0, 3.0, 3.0, 1.5, 1.5}}};
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
        // Local WCS conversion and DS9 decimal precision can leave a small angle error near 0/360 degrees.
        const double rotation_tolerance = rs0.type == CARTA::ANNANNULUS ? 1e-3 : 1e-5;
        if (std::fabs(std::remainder(rs0.rotation - rs1.rotation, 360.0)) > rotation_tolerance) {
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

TEST_F(RegionImportExportTest, TestDs9AnnotationAnnulusImport) {
    auto image_path = FitsImages() / "noise_10px_10px.fits";
    auto loader = carta::FileLoader::GetLoader(image_path);
    std::shared_ptr<Frame> frame(new Frame(0, loader, "0"));
    carta::RegionHandler region_handler;
    const std::string contents = "image\n# ellipse(5,5,3,4,1.5,2,0)\n";
    CARTA::ImportRegionAck import_ack;

    region_handler.ImportRegion(0, frame, CARTA::DS9_REG, contents, false, import_ack);

    ASSERT_TRUE(import_ack.success()) << import_ack.message();
    ASSERT_EQ(import_ack.regions_size(), 1);
    EXPECT_EQ(import_ack.regions().begin()->second.region_type(), CARTA::ANNANNULUS);
}

TEST_F(RegionImportExportTest, TestInvalidAnnulusImportReportsReason) {
    auto loader = carta::FileLoader::GetLoader(FitsImages() / "noise_10px_10px.fits");
    auto frame = std::make_shared<Frame>(0, loader, "0");
    for (bool include_valid_region : {false, true}) {
        carta::RegionHandler handler;
        CARTA::ImportRegionAck ack;
        std::string contents = "image\nellipse(100,100,20,10,40,30)\n";
        if (include_valid_region)
            contents += "circle(5,5,2)\n";
        handler.ImportRegion(0, frame, CARTA::DS9_REG, contents, false, ack);
        EXPECT_EQ(ack.success(), include_valid_region);
        EXPECT_EQ(ack.regions_size(), include_valid_region ? 1 : 0);
        EXPECT_THAT(ack.message(), testing::HasSubstr("different inner/outer axis ratios"));
    }
    carta::RegionHandler handler;
    CARTA::ImportRegionAck ack;
    handler.ImportRegion(0, frame, CARTA::DS9_REG, "image\nannulus(5,5,2,2)\n", false, ack);
    EXPECT_FALSE(ack.success());
    EXPECT_THAT(ack.message(), testing::HasSubstr("Invalid annulus geometry"));
    handler.ImportRegion(0, frame, CARTA::DS9_REG, "image\nannulus(5,5,1,2,3)\n", false, ack);
    EXPECT_FALSE(ack.success());
    EXPECT_THAT(ack.message(), testing::HasSubstr("exactly two radii"));
    EXPECT_THAT(ack.message(), testing::HasSubstr("multiple rings"));
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
