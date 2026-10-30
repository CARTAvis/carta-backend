/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <casacore/coordinates/Coordinates/ObsInfo.h>
#include <casacore/images/Images/ImageInfo.h>
#include <casacore/measures/Measures/MPosition.h>

#include "CommonTestUtilities.h"
#include "FileList/FileExtInfoLoader.h"
#include "FileList/FileInfoLoader.h"
#include "ImageData/CartaZarrImage.h"
#include "ImageData/FileLoader.h"
#include "ImageData/ZarrStores.h"

using namespace carta;

namespace {

const std::filesystem::path kZarrFixture{ZARR_PIXEL_FIXTURE};

constexpr int kWidth = 4;
constexpr int kHeight = 5;
constexpr int kDepth = 2;
constexpr int kStokes = 3;

class ZarrImageTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!std::filesystem::exists(kZarrFixture)) {
            GTEST_SKIP() << "carta-zarr pixel fixture not found at " << kZarrFixture;
        }
    }

    void TearDown() override {
        ZarrStores::Instance().Clear();
    }
};

// The image a file loader opens from `store`, or null if it opens none
FileLoader::ImageRef ImageThroughLoader(const std::filesystem::path& store) {
    auto loader = FileLoader::GetLoader(store.string());
    if (!loader) {
        return nullptr;
    }
    loader->OpenFile("");
    return loader->GetImage();
}

// A copy of the pixel fixture whose SKY metadata is rewritten by `edit`.
std::filesystem::path EditedFixture(const std::string& name, const std::function<std::string(const std::string&)>& edit) {
    const auto copy = TestRoot() / "data" / "generated" / name;
    std::filesystem::remove_all(copy);
    std::filesystem::copy(kZarrFixture, copy, std::filesystem::copy_options::recursive);
    const auto metadata = copy / "SKY" / "zarr.json";
    std::stringstream text;
    text << std::ifstream(metadata).rdbuf();
    std::ofstream(metadata, std::ios::trunc) << edit(text.str());
    return copy;
}

std::vector<std::string> ListedImages(const std::filesystem::path& store) {
    ZarrStores::Instance().Clear();
    CARTA::FileInfo file_info;
    FileInfoLoader(store.string(), CARTA::FileType::ZARR).FillFileInfo(file_info);
    return {file_info.hdu_list().begin(), file_info.hdu_list().end()};
}

} // namespace

TEST_F(ZarrImageTest, LoaderIsSelectedAndShapeIsCarta) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    const auto shape = loader->GetShape();
    ASSERT_EQ(shape.size(), 4);
    EXPECT_EQ(shape(0), kWidth);
    EXPECT_EQ(shape(1), kHeight);
    EXPECT_EQ(shape(2), kDepth);
    EXPECT_EQ(shape(3), kStokes);
}

// The storage layout lists CARTA's four axes in CARTA's order, without the time axis: the fixture is chunked
// one plane at a time, two l by five m
TEST_F(ZarrImageTest, TheStorageLayoutIsInCartasOrder) {
    CartaZarrImage image(kZarrFixture.string(), "SKY");
    const auto entries = image.GetStorageInfo();
    const auto chunk = std::find_if(entries.begin(), entries.end(), [](const StorageEntry& entry) { return entry.name == "Chunk shape"; });
    ASSERT_NE(chunk, entries.end());
    EXPECT_EQ(chunk->value, "[2, 5, 1, 1]");
    EXPECT_TRUE(chunk->has_shape);
}

TEST_F(ZarrImageTest, TelescopePositionIsCartesian) {
    auto image = ImageThroughLoader(kZarrFixture);
    ASSERT_NE(image, nullptr);

    const auto position = image->coordinates().obsInfo().telescopePosition();
    const auto value = position.getValue().getValue(); // meters, x y z
    ASSERT_EQ(value.size(), 3);
    // longitude 2.0 rad, latitude -0.5 rad, radius 6371000 m
    EXPECT_NEAR(value(0), -2326709.631, 1.0);
    EXPECT_NEAR(value(1), 5083953.295, 1.0);
    EXPECT_NEAR(value(2), -3054420.106, 1.0);
}

// A sky image's sub_type is its image type, as BTYPE is a FITS image's
TEST_F(ZarrImageTest, ASkyImagesSubTypeIsItsImageType) {
    const auto copy = EditedFixture("spectral_index.zarr", [](const std::string& text) {
        return std::regex_replace(text, std::regex(R"("type"\s*:\s*"sky")"), R"("type": "sky", "sub_type": "SpectralIndex")");
    });
    auto image = ImageThroughLoader(copy);
    ASSERT_NE(image, nullptr);
    EXPECT_EQ(image->imageInfo().imageType(), casacore::ImageInfo::SpectralIndex);
}

// An image's unit is normalized as FileLoader normalizes every image's. XRADIO copies BUNIT unchanged, and an
// upper-case FITS spelling, which casacore::Unit refuses, kept a store converted from such a file from opening
// while the file itself opened. A unit casacore does not know is left out, and the image still opens.
TEST_F(ZarrImageTest, AUnitIsNormalizedAndAnUnknownOneLeftOut) {
    for (const auto& [written, read] : {std::pair<std::string, std::string>{"JY/BEAM", "Jy/beam"},
             std::pair<std::string, std::string>{"JY/PIXEL", "Jy/pixel"}, std::pair<std::string, std::string>{"wombats", ""}}) {
        // The same path each time, so that no probe of the last copy is kept
        ZarrStores::Instance().Clear();
        const auto copy = EditedFixture("unit.zarr", [&](const std::string& text) {
            return std::regex_replace(
                text, std::regex(R"("units"\s*:\s*"Jy/beam")"), "\"units\": \"" + written + "\"", std::regex_constants::format_first_only);
        });
        auto image = ImageThroughLoader(copy);
        ASSERT_NE(image, nullptr) << written;
        EXPECT_EQ(std::string(image->units().getName()), read) << written;
    }
}

TEST_F(ZarrImageTest, BeamsThatAgreeOnEveryPlaneBecomeOne) {
    const std::filesystem::path fixture{ZARR_XRADIO_UNIFORM_BEAM_FIXTURE};
    if (!std::filesystem::exists(fixture)) {
        GTEST_SKIP() << "uniform-beam fixture not found at " << fixture;
    }
    auto image = ImageThroughLoader(fixture);
    ASSERT_NE(image, nullptr);

    const auto& info = image->imageInfo();
    ASSERT_TRUE(info.hasBeam());
    EXPECT_FALSE(info.hasMultipleBeams()) << "every plane carries the same beam";
    ASSERT_TRUE(info.hasSingleBeam());

    const auto beam = info.restoringBeam();
    EXPECT_DOUBLE_EQ(beam.getMajor().getValue("rad"), 2.0e-5);
    EXPECT_DOUBLE_EQ(beam.getMinor().getValue("rad"), 1.0e-5);
    EXPECT_DOUBLE_EQ(beam.getPA().getValue("rad"), 0.1);
}

TEST_F(ZarrImageTest, BeamsThatDifferPerPlaneStayMany) {
    const std::filesystem::path fixture{ZARR_XRADIO_FIXTURE};
    if (!std::filesystem::exists(fixture)) {
        GTEST_SKIP() << "xradio fixture not found at " << fixture;
    }
    auto image = ImageThroughLoader(fixture);
    ASSERT_NE(image, nullptr);

    const auto& info = image->imageInfo();
    ASSERT_TRUE(info.hasBeam());
    EXPECT_TRUE(info.hasMultipleBeams()) << "the planes carry different beams";
    EXPECT_EQ(info.getBeamSet().nelements(), 6u); // three channels by two polarizations
}

// Every image in the file list is one a user can open.
TEST_F(ZarrImageTest, EveryListedImageOpens) {
    const std::filesystem::path fixture{ZARR_XRADIO_FIXTURE};
    if (!std::filesystem::exists(fixture)) {
        GTEST_SKIP() << "xradio fixture not found at " << fixture;
    }
    const auto listed = ListedImages(fixture);
    ASSERT_FALSE(listed.empty());
    for (const auto& id : listed) {
        EXPECT_NO_THROW(CartaZarrImage(fixture.string(), id)) << id << " was listed";
    }
}

// XRADIO stores a flag as a Zarr bool and names it as its image's flag in the root's data_groups. The
// fixture was converted by XRADIO itself from two masked CASA images, so each image should be listed,
// open without a pixel-mask note, and find its own flag.
TEST_F(ZarrImageTest, EachImageFindsTheFlagXradioWroteForIt) {
    const std::filesystem::path fixture{ZARR_XRADIO_FLAGGED_FIXTURE};
    if (!std::filesystem::exists(fixture)) {
        GTEST_SKIP() << "xradio flagged fixture not found at " << fixture;
    }
    auto listed = ListedImages(fixture);
    std::sort(listed.begin(), listed.end());
    EXPECT_EQ(listed, (std::vector<std::string>{"SKY", "SKY_OTHER"}));

    const auto probe = ZarrStores::Instance().Probe(fixture.string());
    ASSERT_TRUE(probe->dataset) << probe->why_not_openable;
    for (const auto& [image_id, flag_id] : {std::pair{"SKY", "FLAG_SKY"}, std::pair{"SKY_OTHER", "FLAG_SKY_OTHER"}}) {
        const CartaZarrImage image(fixture.string(), image_id);
        for (const auto& note : image.Notes()) {
            EXPECT_EQ(note.brief.find("pixel_mask"), std::string::npos) << image_id << ": " << note.brief;
        }
        const auto opened = probe->dataset->OpenImage(image_id);
        ASSERT_TRUE(opened) << image_id << ": " << opened.error().message;
        EXPECT_EQ(opened.value().descriptor().pixel_mask_id, flag_id) << image_id;
    }
}

// CASA's ia.fft writes the aperture plane of an image, and XRADIO converts it with u and v in place of l and m.
// It is listed, opens with a linear coordinate named UU and VV where a sky image has its direction, and has
// file info.
TEST_F(ZarrImageTest, AnApertureImageOpensWithUuAndVv) {
    const std::filesystem::path fixture{ZARR_XRADIO_APERTURE_FIXTURE};
    if (!std::filesystem::exists(fixture)) {
        GTEST_SKIP() << "xradio aperture fixture not found at " << fixture;
    }
    EXPECT_EQ(ListedImages(fixture), (std::vector<std::string>{"APERTURE"}));

    const CartaZarrImage image(fixture.string(), "APERTURE");
    EXPECT_EQ(image.shape(), casacore::IPosition(4, 5, 4, 2, 3));
    const auto& coordinates = image.coordinates();
    EXPECT_FALSE(coordinates.hasDirectionCoordinate());
    ASSERT_TRUE(coordinates.hasLinearCoordinate());
    const auto names = coordinates.linearCoordinate(coordinates.findCoordinate(casacore::Coordinate::LINEAR)).worldAxisNames();
    EXPECT_EQ(names[0], "UU");
    EXPECT_EQ(names[1], "VV");
    EXPECT_TRUE(image.imageInfo().restoringBeam().isNull()) << "the aperture plane has no beam";

    auto loader = FileLoader::GetLoader(fixture.string());
    ASSERT_NE(loader, nullptr);
    CARTA::FileInfoExtended extended_info;
    std::string message;
    EXPECT_TRUE(FileExtInfoLoader(loader).FillFileExtInfo(extended_info, fixture.filename().string(), "APERTURE", message)) << message;
    EXPECT_EQ(extended_info.width(), 5);
    EXPECT_EQ(extended_info.height(), 4);
}

// The library opens every image of a dataset with two times, but CARTA displays one time, so none is
// offered, and opening one anyway says why.
TEST_F(ZarrImageTest, ADatasetWithTwoTimesListsNoImage) {
    const std::filesystem::path fixture{ZARR_XRADIO_TIME_AXIS_FIXTURE};
    if (!std::filesystem::exists(fixture)) {
        GTEST_SKIP() << "xradio time-axis fixture not found at " << fixture;
    }
    EXPECT_TRUE(ListedImages(fixture).empty()) << "an image CARTA cannot open should not be offered";

    CARTA::FileInfo file_info;
    FileInfoLoader loader(fixture.string());
    EXPECT_FALSE(loader.FillFileInfo(file_info));
    EXPECT_NE(loader.Message().find("singleton time axis"), std::string::npos) << loader.Message();

    try {
        CartaZarrImage image(fixture.string());
        ADD_FAILURE() << "an image with two times should not open";
    } catch (const casacore::AipsError& error) {
        EXPECT_NE(error.getMesg().find("singleton time axis"), std::string::npos) << error.getMesg();
    }
}
