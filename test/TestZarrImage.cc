/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <casacore/coordinates/Coordinates/ObsInfo.h>
#include <casacore/images/Images/ImageInfo.h>
#include <casacore/images/Images/SubImage.h>
#include <casacore/lattices/LRegions/LCBox.h>
#include <casacore/lattices/LRegions/LCPixelSet.h>
#include <casacore/lattices/Lattices/LatticeConcat.h>
#include <casacore/measures/Measures/MPosition.h>

#include "CommonTestUtilities.h"
#include "FileList/FileExtInfoLoader.h"
#include "FileList/FileInfoLoader.h"
#include "ImageData/CartaZarrImage.h"
#include "ImageData/FileLoader.h"
#include "ImageData/ZarrLoader.h"
#include "ImageData/ZarrStores.h"
#include "ImageStats/StatsCalculator.h"

using namespace carta;

namespace {

const std::filesystem::path kZarrFixture{ZARR_PIXEL_FIXTURE};

constexpr int kWidth = 4;
constexpr int kHeight = 5;
constexpr int kDepth = 2;
constexpr int kStokes = 3;

// The pixel fixture's values say where they are, and a third of its pixels are flagged
float ExpectedValue(int x, int y, int z, int stokes) {
    return static_cast<float>((z * 1000) + (stokes * 100) + (x * 10) + y);
}

// The chunk the fixture leaves unwritten, which reads as NaN
bool InMissingChunk(int x, int z, int stokes) {
    return z == 1 && stokes == 2 && x >= 2;
}

bool ExpectedFlag(int x, int y) {
    return ((x + y) % 3) != 0;
}

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

TEST_F(ZarrImageTest, GetSliceReturnsCorrectPixels) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");

    for (int stokes = 0; stokes < kStokes; ++stokes) {
        for (int z = 0; z < kDepth; ++z) {
            casacore::Slicer slicer(casacore::IPosition(4, 0, 0, z, stokes), casacore::IPosition(4, kWidth, kHeight, 1, 1));
            casacore::Array<float> data(slicer.length());
            ASSERT_TRUE(loader->GetSlice(data, StokesSlicer(StokesSource(), slicer))) << "z=" << z << " stokes=" << stokes;
            ASSERT_EQ(data.nelements(), kWidth * kHeight);

            for (int y = 0; y < kHeight; ++y) {
                for (int x = 0; x < kWidth; ++x) {
                    const float value = data(casacore::IPosition(4, x, y, 0, 0));
                    if (InMissingChunk(x, z, stokes) || !ExpectedFlag(x, y)) {
                        EXPECT_TRUE(std::isnan(value)) << "x=" << x << " y=" << y << " z=" << z << " stokes=" << stokes;
                    } else {
                        EXPECT_FLOAT_EQ(value, ExpectedValue(x, y, z, stokes))
                            << "x=" << x << " y=" << y << " z=" << z << " stokes=" << stokes;
                    }
                }
            }
        }
    }
}

TEST_F(ZarrImageTest, GetSliceHonorsASubRegion) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");

    casacore::Slicer slicer(casacore::IPosition(4, 1, 2, 0, 1), casacore::IPosition(4, 2, 3, 1, 1));
    casacore::Array<float> data(slicer.length());
    ASSERT_TRUE(loader->GetSlice(data, StokesSlicer(StokesSource(), slicer)));
    ASSERT_EQ(data.nelements(), 6);

    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 2; ++x) {
            const float value = data(casacore::IPosition(4, x, y, 0, 0));
            if (!ExpectedFlag(x + 1, y + 2)) {
                EXPECT_TRUE(std::isnan(value));
            } else {
                EXPECT_FLOAT_EQ(value, ExpectedValue(x + 1, y + 2, 0, 1));
            }
        }
    }
}

// The mask casacore sees is the finiteness of the pixels, which is how a flagged or absent pixel
// reaches every casacore consumer that has no NaN handling of its own
TEST_F(ZarrImageTest, MaskMarksFlaggedPixels) {
    auto image = ImageThroughLoader(kZarrFixture);
    ASSERT_NE(image, nullptr);
    EXPECT_TRUE(image->isMasked());
    EXPECT_TRUE(image->hasPixelMask());

    casacore::Slicer slicer(casacore::IPosition(4, 0, 0, 0, 0), casacore::IPosition(4, kWidth, kHeight, 1, 1));
    casacore::Array<bool> mask(slicer.length());
    image->getMaskSlice(mask, slicer);
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            EXPECT_EQ(mask(casacore::IPosition(4, x, y, 0, 0)), ExpectedFlag(x, y)) << "x=" << x << " y=" << y;
        }
    }
}

// Seven of the plane's twenty pixels are flagged, so the statistics are over thirteen summing to 217.
// casacore's statistics exclude a NaN only when the image reports a mask.
TEST_F(ZarrImageTest, PlaneStatisticsExcludeFlaggedPixels) {
    auto image = ImageThroughLoader(kZarrFixture);
    ASSERT_NE(image, nullptr);

    const casacore::Slicer plane(casacore::IPosition(4, 0, 0, 0, 0), casacore::IPosition(4, kWidth, kHeight, 1, 1));
    casacore::SubImage<float> sub_image(*image, casacore::LCBox(plane, image->shape()), false);

    const std::vector<CARTA::StatsType> requested{
        CARTA::StatsType::NumPixels, CARTA::StatsType::Sum, CARTA::StatsType::Mean, CARTA::StatsType::Min, CARTA::StatsType::Max};
    std::map<CARTA::StatsType, std::vector<double>> stats;
    ASSERT_TRUE(CalcStatsValues(stats, requested, sub_image, false));

    EXPECT_DOUBLE_EQ(stats[CARTA::StatsType::NumPixels][0], 13.0);
    EXPECT_DOUBLE_EQ(stats[CARTA::StatsType::Sum][0], 217.0);
    EXPECT_NEAR(stats[CARTA::StatsType::Mean][0], 217.0 / 13.0, 1e-9);
    EXPECT_DOUBLE_EQ(stats[CARTA::StatsType::Min][0], 1.0);
    EXPECT_DOUBLE_EQ(stats[CARTA::StatsType::Max][0], 34.0);
}

// The mask kept from a pixel read agrees with one read on its own
TEST_F(ZarrImageTest, CachedMaskAgreesWithADirectMaskRead) {
    auto image = ImageThroughLoader(kZarrFixture);
    ASSERT_NE(image, nullptr);

    const casacore::Slicer slicer(casacore::IPosition(4, 0, 0, 1, 2), casacore::IPosition(4, kWidth, kHeight, 1, 1));
    casacore::Array<bool> direct(slicer.length());
    image->getMaskSlice(direct, slicer);

    casacore::Array<float> pixels(slicer.length());
    image->getSlice(pixels, slicer);
    casacore::Array<bool> cached(slicer.length());
    image->getMaskSlice(cached, slicer);

    EXPECT_TRUE(allEQ(direct, cached));
    // This plane holds the missing chunk, whose pixels are absent rather than flagged
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            EXPECT_EQ(cached(casacore::IPosition(4, x, y, 0, 0)), ExpectedFlag(x, y) && !InMissingChunk(x, 1, 2))
                << "x=" << x << " y=" << y;
        }
    }
}

// A section's mask is kept for the mask read that follows, and let go once read
TEST_F(ZarrImageTest, AKeptMaskIsReleasedOnceItIsRead) {
    CartaZarrImage image(kZarrFixture.string());
    const casacore::Slicer cube(casacore::IPosition(4, 0, 0, 0, 0), casacore::IPosition(4, kWidth, kHeight, 2, 1));
    casacore::Array<float> pixels;
    image.getSlice(pixels, cube);
    EXPECT_EQ(image.MaskCacheBytes(), static_cast<std::size_t>(kWidth * kHeight * 2));

    casacore::Array<bool> mask;
    image.getMaskSlice(mask, cube);
    EXPECT_EQ(image.MaskCacheBytes(), 0u);
    casacore::Array<bool> again;
    image.getMaskSlice(again, cube);
    EXPECT_TRUE(allEQ(mask, again));
}

// A strided section reads every other pixel, and its kept mask is not handed to the section of the same start
// and length at unit stride
TEST_F(ZarrImageTest, AStridedSectionKeepsItsOwnMask) {
    CartaZarrImage image(kZarrFixture.string());
    const casacore::Slicer strided(
        casacore::IPosition(4, 0, 0, 0, 0), casacore::IPosition(4, 2, 3, 1, 1), casacore::IPosition(4, 2, 2, 1, 1));
    casacore::Array<float> pixels;
    image.getSlice(pixels, strided);
    ASSERT_EQ(pixels.shape(), casacore::IPosition(4, 2, 3, 1, 1));
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 2; ++x) {
            const float value = pixels(casacore::IPosition(4, x, y, 0, 0));
            if (ExpectedFlag(2 * x, 2 * y)) {
                EXPECT_FLOAT_EQ(value, ExpectedValue(2 * x, 2 * y, 0, 0)) << "x=" << x << " y=" << y;
            } else {
                EXPECT_TRUE(std::isnan(value)) << "x=" << x << " y=" << y;
            }
        }
    }

    const casacore::Slicer unit(casacore::IPosition(4, 0, 0, 0, 0), casacore::IPosition(4, 2, 3, 1, 1));
    casacore::Array<bool> mask;
    image.getMaskSlice(mask, unit);
    EXPECT_EQ(image.MaskCacheBytes(), 6u) << "the strided section's mask was handed to another section";
    image.getMaskSlice(mask, strided);
    EXPECT_EQ(image.MaskCacheBytes(), 0u);
}

// A read into part of a larger array fills that part and leaves the rest alone, for the pixels and for a mask
// handed back from the read
TEST_F(ZarrImageTest, AReadIntoPartOfALargerArrayFillsOnlyThatPart) {
    CartaZarrImage image(kZarrFixture.string());
    const casacore::Slicer plane(casacore::IPosition(4, 0, 0, 0, 0), casacore::IPosition(4, kWidth, kHeight, 1, 1));
    const casacore::IPosition inner_start(4, 1, 1, 0, 0);
    const casacore::IPosition inner_end(4, kWidth, kHeight, 0, 0);

    casacore::Array<float> pixels(casacore::IPosition(4, kWidth + 2, kHeight + 2, 1, 1), -1.0f);
    casacore::Array<float> pixel_window = pixels(inner_start, inner_end);
    image.getSlice(pixel_window, plane);
    casacore::Array<bool> mask(casacore::IPosition(4, kWidth + 2, kHeight + 2, 1, 1), false);
    casacore::Array<bool> mask_window = mask(inner_start, inner_end);
    image.getMaskSlice(mask_window, plane);
    EXPECT_EQ(image.MaskCacheBytes(), 0u) << "the mask was read again rather than handed back";

    for (int y = -1; y <= kHeight; ++y) {
        for (int x = -1; x <= kWidth; ++x) {
            const casacore::IPosition at(4, x + 1, y + 1, 0, 0);
            if (x < 0 || y < 0 || x == kWidth || y == kHeight) {
                EXPECT_FLOAT_EQ(pixels(at), -1.0f) << "x=" << x << " y=" << y;
                EXPECT_FALSE(mask(at)) << "x=" << x << " y=" << y;
            } else if (ExpectedFlag(x, y)) {
                EXPECT_FLOAT_EQ(pixels(at), ExpectedValue(x, y, 0, 0)) << "x=" << x << " y=" << y;
                EXPECT_TRUE(mask(at)) << "x=" << x << " y=" << y;
            } else {
                EXPECT_TRUE(std::isnan(pixels(at))) << "x=" << x << " y=" << y;
                EXPECT_FALSE(mask(at)) << "x=" << x << " y=" << y;
            }
        }
    }
}

// A loader's masked read asks for each cursor's pixels and then its mask, and every mask is the one just
// kept: none is left over
TEST_F(ZarrImageTest, ALoaderReadUsesEveryKeptMask) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    const auto* image = dynamic_cast<const CartaZarrImage*>(loader->GetImage().get());
    ASSERT_NE(image, nullptr);

    const casacore::Slicer cube(casacore::IPosition(4, 0, 0, 0, 0), casacore::IPosition(4, kWidth, kHeight, kDepth, 1));
    casacore::Array<float> data(cube.length());
    ASSERT_TRUE(loader->GetSlice(data, StokesSlicer(StokesSource(), cube)));
    EXPECT_EQ(image->MaskCacheBytes(), 0u);
}

// A chunk that will not decode fails the read with a casacore error, and the rest of the image still reads
TEST_F(ZarrImageTest, AChunkThatWillNotDecodeFailsOnlyItsRead) {
    const auto copy = EditedFixture("broken_chunk.zarr", [](const std::string& text) { return text; });
    const auto chunk = copy / "SKY" / "c" / "0" / "0" / "0" / "0" / "0";
    const auto size = std::filesystem::file_size(chunk);
    std::filesystem::resize_file(chunk, size / 2);

    CartaZarrImage image(copy.string());
    casacore::Array<float> pixels;
    const casacore::Slicer broken(casacore::IPosition(4, 0, 0, 0, 0), casacore::IPosition(4, kWidth, kHeight, 1, 1));
    try {
        image.getSlice(pixels, broken);
        ADD_FAILURE() << "a truncated chunk was read";
    } catch (const casacore::AipsError& error) {
        EXPECT_NE(error.getMesg().find("doGetSlice"), std::string::npos) << error.getMesg();
    }

    const casacore::Slicer whole(casacore::IPosition(4, 0, 0, 1, 0), casacore::IPosition(4, kWidth, kHeight, 1, 1));
    image.getSlice(pixels, whole);
    EXPECT_FLOAT_EQ(pixels(casacore::IPosition(4, 1, 0, 0, 0)), ExpectedValue(1, 0, 1, 0));
}

// Two threads reading their own planes of one image each get their own pixels and masks
TEST_F(ZarrImageTest, ThreadsReadingOneImageGetTheirOwnPlanes) {
    CartaZarrImage image(kZarrFixture.string());
    const auto read_plane = [&](int z, int& wrong) {
        const casacore::Slicer plane(casacore::IPosition(4, 0, 0, z, 0), casacore::IPosition(4, kWidth, kHeight, 1, 1));
        for (int round = 0; round < 50; ++round) {
            casacore::Array<float> pixels;
            image.getSlice(pixels, plane);
            casacore::Array<bool> mask;
            image.getMaskSlice(mask, plane);
            for (int y = 0; y < kHeight; ++y) {
                for (int x = 0; x < kWidth; ++x) {
                    const casacore::IPosition at(4, x, y, 0, 0);
                    const bool good = ExpectedFlag(x, y);
                    if (mask(at) != good || (good && pixels(at) != ExpectedValue(x, y, z, 0))) {
                        ++wrong;
                    }
                }
            }
        }
    };
    int wrong_0 = 0;
    int wrong_1 = 0;
    std::thread first(read_plane, 0, std::ref(wrong_0));
    std::thread second(read_plane, 1, std::ref(wrong_1));
    first.join();
    second.join();
    EXPECT_EQ(wrong_0, 0);
    EXPECT_EQ(wrong_1, 0);
}

// The whole mask is a lattice of its own, so that casacore can concatenate Zarr images, as the Stokes files
// of a hypercube are, and keep reading the mask after the images it was given are gone
TEST_F(ZarrImageTest, TheMaskIsALatticeThatOutlivesTheImage) {
    casacore::LatticeConcat<float> concat(3);
    {
        CartaZarrImage first(kZarrFixture.string());
        CartaZarrImage second(kZarrFixture.string());
        EXPECT_FALSE(first.pixelMask().isWritable());
        EXPECT_EQ(first.pixelMask().shape(), first.shape());
        concat.setLattice(first);
        concat.setLattice(second);
    }
    ASSERT_TRUE(concat.hasPixelMask());

    const casacore::Slicer plane(casacore::IPosition(4, 0, 0, 0, kStokes), casacore::IPosition(4, kWidth, kHeight, 1, 1));
    casacore::Array<bool> mask;
    concat.getMaskSlice(mask, plane);
    // Through the concatenation's own mask lattice, which reads the views it was given
    casacore::Array<bool> from_lattice;
    concat.pixelMask().getSlice(from_lattice, plane);
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            EXPECT_EQ(mask(casacore::IPosition(4, x, y, 0, 0)), ExpectedFlag(x, y)) << "x=" << x << " y=" << y;
            EXPECT_EQ(from_lattice(casacore::IPosition(4, x, y, 0, 0)), ExpectedFlag(x, y)) << "x=" << x << " y=" << y;
        }
    }
}

// The fixture is chunked (l, m) = (2, 5), one plane per chunk. The cursor is whole chunks, grown within
// the advised maximum (here up to the whole plane), rather than casacore's default of one row.
TEST_F(ZarrImageTest, NiceCursorShapeIsTheChunk) {
    auto image = ImageThroughLoader(kZarrFixture);
    ASSERT_NE(image, nullptr);

    const auto cursor = image->niceCursorShape(image->advisedMaxPixels());
    ASSERT_EQ(cursor.size(), 4);
    EXPECT_EQ(cursor(0), kWidth);
    EXPECT_EQ(cursor(1), kHeight);
    EXPECT_EQ(cursor(2), 1);
    EXPECT_EQ(cursor(3), 1);
    EXPECT_LE(cursor.product(), static_cast<casacore::Int>(image->advisedMaxPixels()));
}

// A chunk declared longer than an int is clipped to the image, not narrowed to a zero step
TEST_F(ZarrImageTest, NiceCursorShapeClipsAChunkLongerThanAnInt) {
    for (const char* extent : {"4294967296", "3000000000"}) {
        const auto copy = EditedFixture(std::string("wide_chunk_") + extent + ".zarr", [&](const std::string& text) {
            return std::regex_replace(text, std::regex(R"("chunk_shape"\s*:\s*\[\s*1\s*,\s*1\s*,\s*1\s*,\s*2\s*,)"),
                std::string(R"("chunk_shape": [1, 1, 1, )") + extent + ",");
        });
        ZarrStores::Instance().Clear();
        auto image = ImageThroughLoader(copy);
        ASSERT_NE(image, nullptr) << "a chunk " << extent << " long was not opened";
        const auto cursor = image->niceCursorShape(image->advisedMaxPixels());
        ASSERT_EQ(cursor.size(), 4);
        EXPECT_EQ(cursor(0), kWidth) << "a chunk " << extent << " long";
        EXPECT_EQ(cursor(1), kHeight) << "a chunk " << extent << " long";
        EXPECT_EQ(cursor(2), 1);
        EXPECT_EQ(cursor(3), 1);
    }
}

// A cursor's spectrum read by the loader is the column casacore reads, and a one-pixel read reports the
// channels that are final as it goes
TEST_F(ZarrImageTest, CursorSpectralDataIsTheColumnAndReportsProgress) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    auto image = loader->GetImage();
    ASSERT_NE(image, nullptr);

    for (int stokes = 0; stokes < kStokes; ++stokes) {
        std::mutex image_mutex;
        std::vector<float> data;
        std::size_t last_done = 0;
        std::size_t last_total = 0;
        int calls = 0;
        casacore::Array<float> column;
        image->doGetSlice(column, casacore::Slicer(casacore::IPosition(4, 2, 3, 0, stokes), casacore::IPosition(4, 1, 1, kDepth, 1)));
        const auto same_as_column = [&](std::size_t channels) {
            for (std::size_t z = 0; z < channels; ++z) {
                const float expected = column(casacore::IPosition(4, 0, 0, z, 0));
                if (std::isnan(expected) ? !std::isnan(data[z]) : data[z] != expected) {
                    return false;
                }
            }
            return true;
        };

        ASSERT_TRUE(loader->GetCursorSpectralData(data, stokes, 2, 1, 3, 1, image_mutex, [&](std::size_t done, std::size_t total) {
            EXPECT_LE(done, total);
            EXPECT_GE(done, last_done) << "the final part only grows";
            EXPECT_TRUE(same_as_column(done)) << "the channels reported final are the column's, stokes=" << stokes;
            last_done = done;
            last_total = total;
            ++calls;
            return true;
        }));
        EXPECT_GT(calls, 0);
        EXPECT_EQ(last_done, static_cast<std::size_t>(kDepth));
        EXPECT_EQ(last_total, static_cast<std::size_t>(kDepth));
        ASSERT_EQ(data.size(), static_cast<std::size_t>(kDepth));
        EXPECT_TRUE(same_as_column(kDepth)) << "stokes=" << stokes;
    }
}

// A read the caller stops returns false and leaves no spectrum. A read of more than one pixel, whose spectra
// the HDF5 loader lays out channel fastest, or of a pixel outside the image, is refused.
TEST_F(ZarrImageTest, CursorSpectralDataStopsWhenToldAndReadsOnlyOnePixel) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    std::mutex image_mutex;

    std::vector<float> data;
    EXPECT_FALSE(loader->GetCursorSpectralData(data, 0, 2, 1, 3, 1, image_mutex, [](std::size_t, std::size_t) { return false; }));
    EXPECT_TRUE(data.empty());

    EXPECT_FALSE(loader->GetCursorSpectralData(data, 0, 1, 2, 1, 2, image_mutex, {}));
    EXPECT_FALSE(loader->GetCursorSpectralData(data, 0, kWidth, 1, 0, 1, image_mutex, {}));
    EXPECT_FALSE(loader->GetCursorSpectralData(data, kStokes, 0, 1, 0, 1, image_mutex, {}));
}

// A region's spectral profile from the loader is the statistics casacore computes over the same masked
// region, one channel at a time
TEST_F(ZarrImageTest, RegionSpectralDataAgreesWithCasacore) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    auto image = loader->GetImage();
    ASSERT_NE(image, nullptr);

    // A region across the chunk boundary at x = 2, with a mask that is neither empty nor full
    const int x0 = 1, y0 = 1, region_width = 3, region_height = 4;
    casacore::Array<casacore::Bool> mask(casacore::IPosition(4, region_width, region_height, 1, 1));
    casacore::Array<casacore::Bool> mask_2d(casacore::IPosition(2, region_width, region_height));
    for (int y = 0; y < region_height; ++y) {
        for (int x = 0; x < region_width; ++x) {
            const bool in_region = ((x + (2 * y)) % 3) != 0;
            mask(casacore::IPosition(4, x, y, 0, 0)) = in_region;
            mask_2d(casacore::IPosition(2, x, y)) = in_region;
        }
    }
    casacore::ArrayLattice<casacore::Bool> mask_lattice(mask_2d);

    const std::vector<CARTA::StatsType> compared{CARTA::StatsType::NumPixels, CARTA::StatsType::Sum, CARTA::StatsType::Mean,
        CARTA::StatsType::RMS, CARTA::StatsType::Sigma, CARTA::StatsType::Min, CARTA::StatsType::Max};

    for (int stokes = 0; stokes < kStokes; ++stokes) {
        std::mutex image_mutex;
        EXPECT_TRUE(loader->UseRegionSpectralData(casacore::IPosition(2, region_width, region_height), image_mutex));

        std::map<CARTA::StatsType, std::vector<double>> from_loader;
        float progress = 0.0;
        int rounds = 0;
        while (progress < 1.0) {
            ASSERT_TRUE(loader->GetRegionSpectralData(
                stokes, AxisRange(0, kDepth - 1), stokes, mask_lattice, casacore::IPosition(2, x0, y0), image_mutex, from_loader, progress))
                << "stokes=" << stokes;
            ASSERT_LT(++rounds, 100) << "the profile never finished";
        }

        for (int z = 0; z < kDepth; ++z) {
            casacore::LCBox box(casacore::IPosition(4, x0, y0, z, stokes),
                casacore::IPosition(4, x0 + region_width - 1, y0 + region_height - 1, z, stokes), image->shape());
            casacore::SubImage<float> sub_image(*image, casacore::LCPixelSet(mask, box), false);
            std::map<CARTA::StatsType, std::vector<double>> from_casacore;
            ASSERT_TRUE(CalcStatsValues(from_casacore, compared, sub_image, false));

            for (const auto stat : compared) {
                const double loader_value = from_loader[stat][z];
                const double casacore_value = from_casacore[stat][0];
                const std::string where =
                    " stat=" + std::to_string(static_cast<int>(stat)) + " z=" + std::to_string(z) + " stokes=" + std::to_string(stokes);
                if (std::isnan(casacore_value)) {
                    EXPECT_TRUE(std::isnan(loader_value)) << where;
                } else {
                    EXPECT_NEAR(loader_value, casacore_value, 1e-9 * (1.0 + std::abs(casacore_value))) << where;
                }
            }
        }
    }
}

namespace {

// A Zarr loader that hands back a region's profile after every run of channels, as one reading a large image
// does after TARGET_PARTIAL_REGION_TIME
class PausingZarrLoader : public ZarrLoader {
public:
    explicit PausingZarrLoader(const std::string& filename) : ZarrLoader(filename) {
        _region_spectral_time = std::chrono::milliseconds(0);
        _region_spectral_channels_per_emit = 1;
    }
    void ReadFor(std::chrono::milliseconds time) {
        _region_spectral_time = time;
    }
};

// A mask over the 3 x 4 box at (1, 1) of the pixel fixture, `pattern` deciding which pixels are in it
casacore::ArrayLattice<casacore::Bool> BoxMask(const std::function<bool(int x, int y)>& pattern) {
    casacore::Array<casacore::Bool> mask(casacore::IPosition(2, 3, 4));
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 3; ++x) {
            mask(casacore::IPosition(2, x, y)) = pattern(x, y);
        }
    }
    return casacore::ArrayLattice<casacore::Bool>(mask);
}

// The channels of a profile that are finished
int FinishedChannels(std::map<CARTA::StatsType, std::vector<double>>& profile) {
    const auto& pixels = profile[CARTA::StatsType::NumPixels];
    return static_cast<int>(std::count_if(pixels.begin(), pixels.end(), [](double n) { return !std::isnan(n); }));
}

} // namespace

// A profile handed back partway goes on where it stopped, and ends as the profile read in one call
TEST_F(ZarrImageTest, RegionSpectralDataResumesWhereItPaused) {
    const auto mask = BoxMask([](int x, int y) { return (x + y) % 2 == 0; });
    const casacore::IPosition origin(2, 1, 1);
    std::mutex image_mutex;

    auto whole = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(whole, nullptr);
    whole->OpenFile("");
    std::map<CARTA::StatsType, std::vector<double>> expected;
    float progress = 0.0;
    ASSERT_TRUE(whole->GetRegionSpectralData(0, AxisRange(0, kDepth - 1), 0, mask, origin, image_mutex, expected, progress));
    ASSERT_EQ(progress, 1.0f);

    PausingZarrLoader paused(kZarrFixture.string());
    paused.OpenFile("");
    std::map<CARTA::StatsType, std::vector<double>> resumed;
    progress = 0.0;
    int calls = 0;
    while (progress < 1.0f) {
        ASSERT_TRUE(paused.GetRegionSpectralData(0, AxisRange(0, kDepth - 1), 0, mask, origin, image_mutex, resumed, progress));
        EXPECT_EQ(FinishedChannels(resumed), static_cast<int>(std::lround(progress * kDepth))) << "a reported channel is final";
        ASSERT_LT(++calls, 100);
    }
    EXPECT_GT(calls, 1) << "the profile was read in one call, so nothing paused";
    for (const auto& [stat, values] : expected) {
        ASSERT_EQ(resumed[stat].size(), values.size());
        for (std::size_t z = 0; z < values.size(); ++z) {
            if (std::isnan(values[z])) {
                EXPECT_TRUE(std::isnan(resumed[stat][z]));
            } else {
                EXPECT_EQ(resumed[stat][z], values[z]) << "stat=" << static_cast<int>(stat) << " z=" << z;
            }
        }
    }
}

// The stokes of a computed stokes are read in turn with one progress, so no call reads past the channel
// the first stokes reached, and the progress holds for all of them
TEST_F(ZarrImageTest, RegionSpectralDataKeepsTheStokesOfARegionInStep) {
    const auto mask = BoxMask([](int, int) { return true; });
    const casacore::IPosition origin(2, 1, 1);
    std::mutex image_mutex;
    PausingZarrLoader loader(kZarrFixture.string());
    loader.OpenFile("");

    std::map<CARTA::StatsType, std::vector<double>> q, u;
    float q_progress = 0.0, u_progress = 0.0;
    // Q pauses after its first channel; U could read every channel in the time it is given
    ASSERT_TRUE(loader.GetRegionSpectralData(7, AxisRange(0, kDepth - 1), 1, mask, origin, image_mutex, q, q_progress));
    ASSERT_LT(q_progress, 1.0f);
    loader.ReadFor(std::chrono::seconds(60));
    ASSERT_TRUE(loader.GetRegionSpectralData(7, AxisRange(0, kDepth - 1), 2, mask, origin, image_mutex, u, u_progress));
    EXPECT_EQ(u_progress, q_progress);
    EXPECT_EQ(FinishedChannels(u), FinishedChannels(q));

    // The next round finishes both. Q's call still reports less than 1, because its progress counts U, which
    // is still behind; U's call reports 1.
    ASSERT_TRUE(loader.GetRegionSpectralData(7, AxisRange(0, kDepth - 1), 1, mask, origin, image_mutex, q, q_progress));
    EXPECT_LT(q_progress, 1.0f);
    ASSERT_TRUE(loader.GetRegionSpectralData(7, AxisRange(0, kDepth - 1), 2, mask, origin, image_mutex, u, u_progress));
    EXPECT_EQ(u_progress, 1.0f);
    EXPECT_EQ(FinishedChannels(q), kDepth);
    EXPECT_EQ(FinishedChannels(u), kDepth);
}

// A stokes behind another of its region still stops after _region_spectral_time, instead of reading up to
// the other in one call: after U's profile is finished, Q's is still handed back one run at a time
TEST_F(ZarrImageTest, RegionSpectralDataOfAStokesCatchingUpPauses) {
    const auto mask = BoxMask([](int, int) { return true; });
    const casacore::IPosition origin(2, 1, 1);
    std::mutex image_mutex;
    PausingZarrLoader loader(kZarrFixture.string());
    loader.OpenFile("");

    std::map<CARTA::StatsType, std::vector<double>> q, u;
    float q_progress = 0.0, u_progress = 0.0;
    loader.ReadFor(std::chrono::seconds(60));
    ASSERT_TRUE(loader.GetRegionSpectralData(8, AxisRange(0, kDepth - 1), 2, mask, origin, image_mutex, u, u_progress));
    ASSERT_EQ(u_progress, 1.0f);

    loader.ReadFor(std::chrono::milliseconds(0));
    ASSERT_TRUE(loader.GetRegionSpectralData(8, AxisRange(0, kDepth - 1), 1, mask, origin, image_mutex, q, q_progress));
    EXPECT_LT(q_progress, 1.0f);
    EXPECT_EQ(FinishedChannels(q), 1);
    int calls = 1;
    while (q_progress < 1.0f) {
        ASSERT_TRUE(loader.GetRegionSpectralData(8, AxisRange(0, kDepth - 1), 1, mask, origin, image_mutex, q, q_progress));
        ASSERT_LT(++calls, 100);
    }
    EXPECT_EQ(FinishedChannels(q), kDepth);
}

// A region edited within its bounding box has the same origin and shape, and another mask, so its profile is
// read again rather than handed back from before
TEST_F(ZarrImageTest, RegionSpectralDataOfAnotherMaskIsReadAgain) {
    const casacore::IPosition origin(2, 1, 1);
    std::mutex image_mutex;
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");

    const auto profile = [&](const casacore::ArrayLattice<casacore::Bool>& mask) {
        std::map<CARTA::StatsType, std::vector<double>> results;
        float progress = 0.0;
        while (progress < 1.0f) {
            EXPECT_TRUE(loader->GetRegionSpectralData(3, AxisRange(0, kDepth - 1), 0, mask, origin, image_mutex, results, progress));
        }
        return results;
    };
    const auto all = BoxMask([](int, int) { return true; });
    const auto column = BoxMask([](int x, int) { return x == 0; });
    auto first = profile(all);
    auto edited = profile(column);

    // The same mask's profile from a loader that has read nothing before
    loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    auto fresh = profile(column);
    EXPECT_NE(edited[CARTA::StatsType::NumPixels], first[CARTA::StatsType::NumPixels]);
    EXPECT_EQ(edited[CARTA::StatsType::NumPixels], fresh[CARTA::StatsType::NumPixels]);
    EXPECT_EQ(edited[CARTA::StatsType::Sum], fresh[CARTA::StatsType::Sum]);
}

// A region whose bounding box changes size is read again, as any other region is
TEST_F(ZarrImageTest, RegionSpectralDataOfAResizedRegionIsReadAgain) {
    const casacore::IPosition origin(2, 1, 1);
    std::mutex image_mutex;
    const auto profile = [&](const std::shared_ptr<FileLoader>& loader, const casacore::ArrayLattice<casacore::Bool>& mask) {
        std::map<CARTA::StatsType, std::vector<double>> results;
        float progress = 0.0;
        while (progress < 1.0f) {
            EXPECT_TRUE(loader->GetRegionSpectralData(4, AxisRange(0, kDepth - 1), 0, mask, origin, image_mutex, results, progress));
        }
        return results;
    };
    casacore::ArrayLattice<casacore::Bool> small(casacore::Array<casacore::Bool>(casacore::IPosition(2, 2, 2), true));

    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    profile(loader, BoxMask([](int, int) { return true; }));
    auto resized = profile(loader, small);

    auto fresh_loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(fresh_loader, nullptr);
    fresh_loader->OpenFile("");
    auto fresh = profile(fresh_loader, small);
    EXPECT_EQ(resized[CARTA::StatsType::NumPixels], fresh[CARTA::StatsType::NumPixels]);
    EXPECT_EQ(resized[CARTA::StatsType::Sum], fresh[CARTA::StatsType::Sum]);
}

// Going back to a region read before reads every stokes of it again, so that one finished then does not
// report a computed stokes finished while the others are still being read
TEST_F(ZarrImageTest, RegionSpectralDataOfARegionReadBeforeKeepsTheStokesInStep) {
    const auto first = BoxMask([](int, int) { return true; });
    const auto edited = BoxMask([](int x, int) { return x == 0; });
    const casacore::IPosition origin(2, 1, 1);
    std::mutex image_mutex;
    PausingZarrLoader loader(kZarrFixture.string());
    loader.OpenFile("");
    std::map<CARTA::StatsType, std::vector<double>> q, u;
    float q_progress = 0.0, u_progress = 0.0;

    // Q and U of the first mask finished, then Q of the edited one
    loader.ReadFor(std::chrono::seconds(60));
    ASSERT_TRUE(loader.GetRegionSpectralData(5, AxisRange(0, kDepth - 1), 1, first, origin, image_mutex, q, q_progress));
    ASSERT_TRUE(loader.GetRegionSpectralData(5, AxisRange(0, kDepth - 1), 2, first, origin, image_mutex, u, u_progress));
    ASSERT_EQ(u_progress, 1.0f);
    ASSERT_TRUE(loader.GetRegionSpectralData(5, AxisRange(0, kDepth - 1), 1, edited, origin, image_mutex, q, q_progress));

    // Back to the first mask, with Q pausing after its first channel
    loader.ReadFor(std::chrono::milliseconds(0));
    ASSERT_TRUE(loader.GetRegionSpectralData(5, AxisRange(0, kDepth - 1), 1, first, origin, image_mutex, q, q_progress));
    ASSERT_LT(q_progress, 1.0f);
    ASSERT_TRUE(loader.GetRegionSpectralData(5, AxisRange(0, kDepth - 1), 2, first, origin, image_mutex, u, u_progress));
    EXPECT_EQ(u_progress, q_progress);
}

// U is read partway for its own profile, which is then abandoned. When U is next read with Q for a computed
// stokes, its progress counts Q too.
TEST_F(ZarrImageTest, RegionSpectralDataOfAStokesReadPartwayBeforeKeepsInStep) {
    const auto mask = BoxMask([](int, int) { return true; });
    const casacore::IPosition origin(2, 1, 1);
    std::mutex image_mutex;
    PausingZarrLoader loader(kZarrFixture.string());
    loader.OpenFile("");
    std::map<CARTA::StatsType, std::vector<double>> q, u;
    float q_progress = 0.0, u_progress = 0.0;

    // U's own profile, abandoned after its first channel
    ASSERT_TRUE(loader.GetRegionSpectralData(6, AxisRange(0, kDepth - 1), 2, mask, origin, image_mutex, u, u_progress));
    ASSERT_LT(u_progress, 1.0f);

    // Q then U, as for a computed stokes, with U given time to read every channel
    ASSERT_TRUE(loader.GetRegionSpectralData(6, AxisRange(0, kDepth - 1), 1, mask, origin, image_mutex, q, q_progress));
    loader.ReadFor(std::chrono::seconds(60));
    ASSERT_TRUE(loader.GetRegionSpectralData(6, AxisRange(0, kDepth - 1), 2, mask, origin, image_mutex, u, u_progress));
    EXPECT_LE(u_progress, static_cast<float>(FinishedChannels(q)) / kDepth) << "U reported channels Q has not read";

    ASSERT_TRUE(loader.GetRegionSpectralData(6, AxisRange(0, kDepth - 1), 1, mask, origin, image_mutex, q, q_progress));
    ASSERT_TRUE(loader.GetRegionSpectralData(6, AxisRange(0, kDepth - 1), 2, mask, origin, image_mutex, u, u_progress));
    EXPECT_EQ(u_progress, 1.0f);
    EXPECT_EQ(FinishedChannels(q), kDepth);
}

// Two profiles of a region read at once, each on its own thread: one's progress is of the channels it was
// handed, whatever the other read of the same stokes in between
TEST_F(ZarrImageTest, RegionSpectralDataOfTwoProfilesAtOnceReportsWhatEachWasHanded) {
    const auto mask = BoxMask([](int, int) { return true; });
    const casacore::IPosition origin(2, 1, 1);
    std::mutex image_mutex;
    PausingZarrLoader loader(kZarrFixture.string());
    loader.OpenFile("");
    const auto on_a_thread = [](const std::function<void()>& read) { std::thread(read).join(); };

    std::map<CARTA::StatsType, std::vector<double>> a_q, a_u, b_q, b_u;
    float a_progress = 0.0, b_progress = 0.0;
    std::thread profile_a([&] {
        // A reads Q partway
        ASSERT_TRUE(loader.GetRegionSpectralData(8, AxisRange(0, kDepth - 1), 1, mask, origin, image_mutex, a_q, a_progress));
        ASSERT_LT(a_progress, 1.0f);
        // B reads Q and U to the end
        on_a_thread([&] {
            loader.ReadFor(std::chrono::seconds(60));
            ASSERT_TRUE(loader.GetRegionSpectralData(8, AxisRange(0, kDepth - 1), 1, mask, origin, image_mutex, b_q, b_progress));
            ASSERT_TRUE(loader.GetRegionSpectralData(8, AxisRange(0, kDepth - 1), 2, mask, origin, image_mutex, b_u, b_progress));
            EXPECT_EQ(b_progress, 1.0f);
        });
        // A reads U: what it was handed of Q is not finished, whatever B read
        ASSERT_TRUE(loader.GetRegionSpectralData(8, AxisRange(0, kDepth - 1), 2, mask, origin, image_mutex, a_u, a_progress));
        EXPECT_LE(a_progress, static_cast<float>(FinishedChannels(a_q)) / kDepth);
    });
    profile_a.join();
}

// A profile of some channels, as the PV generator asks for, is of those channels only, and a profile of all
// of them is read again
TEST_F(ZarrImageTest, RegionSpectralDataOfSomeChannels) {
    const auto mask = BoxMask([](int, int) { return true; });
    const casacore::IPosition origin(2, 1, 1);
    std::mutex image_mutex;
    ZarrLoader loader(kZarrFixture.string());
    loader.OpenFile("");

    std::map<CARTA::StatsType, std::vector<double>> part, whole;
    float progress = 0.0;
    ASSERT_TRUE(loader.GetRegionSpectralData(9, AxisRange(1, 1), 0, mask, origin, image_mutex, part, progress));
    EXPECT_EQ(progress, 1.0f);
    ASSERT_EQ(part[CARTA::StatsType::Sum].size(), 1u);

    ASSERT_TRUE(loader.GetRegionSpectralData(9, AxisRange(0, kDepth - 1), 0, mask, origin, image_mutex, whole, progress));
    EXPECT_EQ(progress, 1.0f);
    ASSERT_EQ(whole[CARTA::StatsType::Sum].size(), static_cast<std::size_t>(kDepth));
    EXPECT_EQ(FinishedChannels(whole), kDepth);
    EXPECT_DOUBLE_EQ(whole[CARTA::StatsType::Sum][1], part[CARTA::StatsType::Sum][0]);
    EXPECT_DOUBLE_EQ(whole[CARTA::StatsType::NumPixels][1], part[CARTA::StatsType::NumPixels][0]);
}

// A region with no valid pixel counts its flagged ones, and every other statistic is NaN, as the HDF5 loader
// reports. The fixture has no beam, so there is no flux density.
TEST_F(ZarrImageTest, RegionSpectralDataOfOnlyFlaggedPixels) {
    // The box is at (1, 1); a pixel is flagged where (x + y) % 3 == 0
    const auto mask = BoxMask([](int x, int y) { return ((x + 1) + (y + 1)) % 3 == 0; });
    const auto selected = static_cast<double>(casacore::ntrue(mask.asArray()));
    ASSERT_GT(selected, 0.0);
    const casacore::IPosition origin(2, 1, 1);
    std::mutex image_mutex;
    ZarrLoader loader(kZarrFixture.string());
    loader.OpenFile("");

    std::map<CARTA::StatsType, std::vector<double>> profile;
    float progress = 0.0;
    ASSERT_TRUE(loader.GetRegionSpectralData(10, AxisRange(0, kDepth - 1), 0, mask, origin, image_mutex, profile, progress));
    EXPECT_EQ(progress, 1.0f);
    EXPECT_EQ(profile.count(CARTA::StatsType::FluxDensity), 0u);
    for (int z = 0; z < kDepth; ++z) {
        EXPECT_EQ(profile[CARTA::StatsType::NumPixels][z], 0.0) << "z=" << z;
        EXPECT_EQ(profile[CARTA::StatsType::NanCount][z], selected) << "z=" << z;
        for (const auto stat : {CARTA::StatsType::Sum, CARTA::StatsType::Mean, CARTA::StatsType::RMS, CARTA::StatsType::Sigma,
                 CARTA::StatsType::SumSq, CARTA::StatsType::Min, CARTA::StatsType::Max, CARTA::StatsType::Extrema}) {
            EXPECT_TRUE(std::isnan(profile[stat][z])) << "z=" << z << " stat=" << stat;
        }
    }
}

// A computed stokes is made from the stokes the image has, by the caller; asked for directly, or for a stokes
// the image does not have, the loader refuses
TEST_F(ZarrImageTest, RegionSpectralDataRefusesAStokesTheImageDoesNotHave) {
    const auto mask = BoxMask([](int, int) { return true; });
    const casacore::IPosition origin(2, 1, 1);
    std::mutex image_mutex;
    ZarrLoader loader(kZarrFixture.string());
    loader.OpenFile("");

    std::map<CARTA::StatsType, std::vector<double>> profile;
    float progress = 0.0;
    EXPECT_FALSE(loader.GetRegionSpectralData(
        11, AxisRange(0, kDepth - 1), CARTA::PolarizationType::Plinear, mask, origin, image_mutex, profile, progress));
    EXPECT_FALSE(loader.GetRegionSpectralData(11, AxisRange(0, kDepth - 1), kStokes, mask, origin, image_mutex, profile, progress));
}
