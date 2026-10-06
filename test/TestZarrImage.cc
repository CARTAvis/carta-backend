/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include <casacore/coordinates/Coordinates/ObsInfo.h>
#include <casacore/measures/Measures/MPosition.h>

#include "CommonTestUtilities.h"
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
};

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

TEST_F(ZarrImageTest, TelescopePositionIsCartesian) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    auto image = loader->GetImage();
    ASSERT_NE(image, nullptr);

    const auto position = image->coordinates().obsInfo().telescopePosition();
    const auto value = position.getValue().getValue(); // metres, x y z
    ASSERT_EQ(value.size(), 3);
    // longitude 2.0 rad, latitude -0.5 rad, radius 6371000 m
    EXPECT_NEAR(value(0), -2326709.631, 1.0);
    EXPECT_NEAR(value(1), 5083953.295, 1.0);
    EXPECT_NEAR(value(2), -3054420.106, 1.0);
}

TEST_F(ZarrImageTest, AnObservationDateWrittenAsAStringIsTheEpochItNames) {
    const auto copy = EditedFixture("string_obsdate.zarr", [](const std::string& text) {
        const auto dated = std::regex_replace(text, std::regex(R"("data"\s*:\s*59000\.0)"), R"("data": "2020-05-31T12:00:00")");
        return std::regex_replace(dated, std::regex(R"("format"\s*:\s*"MJD")"), R"("format": "ISO")");
    });
    auto loader = FileLoader::GetLoader(copy.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    auto image = loader->GetImage();
    ASSERT_NE(image, nullptr);

    const auto date = image->coordinates().obsInfo().obsDate();
    EXPECT_EQ(date.getRefPtr()->getType(), casacore::MEpoch::UTC);
    EXPECT_NEAR(date.getValue().get(), 59000.5, 1e-9);
}

TEST_F(ZarrImageTest, BeamsThatAgreeOnEveryPlaneBecomeOne) {
    const std::filesystem::path fixture{ZARR_XRADIO_UNIFORM_BEAM_FIXTURE};
    if (!std::filesystem::exists(fixture)) {
        GTEST_SKIP() << "uniform-beam fixture not found at " << fixture;
    }
    auto loader = FileLoader::GetLoader(fixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    auto image = loader->GetImage();
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
    auto loader = FileLoader::GetLoader(fixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    auto image = loader->GetImage();
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
    ZarrStores::Instance().Clear();
}
