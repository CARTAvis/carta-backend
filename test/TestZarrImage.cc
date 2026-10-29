/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <limits>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <zstd.h>

#include <casacore/coordinates/Coordinates/ObsInfo.h>
#include <casacore/images/Images/ImageInfo.h>
#include <casacore/images/Images/SubImage.h>
#include <casacore/lattices/LRegions/LCBox.h>
#include <casacore/lattices/Lattices/LatticeConcat.h>
#include <casacore/measures/Measures/MPosition.h>

#include "CommonTestUtilities.h"
#include "FileList/FileExtInfoLoader.h"
#include "FileList/FileInfoLoader.h"
#include "Frame/Frame.h"
#include "ImageData/CartaZarrImage.h"
#include "ImageData/FileLoader.h"
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

// An unflagged infinity must be invalid on the direct raster path, just as it is in casacore's mask.
TEST_F(ZarrImageTest, LoaderConvertsBothInfinitiesToNan) {
    const auto copy = EditedFixture("infinite_pixels.zarr", [](const std::string& text) { return text; });
    // The fixture's first chunk is stored in (l, m) order, with m fastest, as little-endian float32 + zstd.
    std::array<unsigned char, 2 * kHeight * sizeof(float)> bytes{};
    for (int x = 0; x < 2; ++x) {
        for (int y = 0; y < kHeight; ++y) {
            float value = ExpectedValue(x, y, 0, 0);
            if (x == 0 && y == 1) {
                value = std::numeric_limits<float>::infinity();
            } else if (x == 0 && y == 2) {
                value = -std::numeric_limits<float>::infinity();
            } else if (x == 1 && y == 1) {
                value = std::numeric_limits<float>::quiet_NaN();
            }
            std::uint32_t bits;
            std::memcpy(&bits, &value, sizeof(bits));
            const auto offset = (x * kHeight + y) * sizeof(float);
            for (std::size_t byte = 0; byte < sizeof(float); ++byte) {
                bytes[offset + byte] = static_cast<unsigned char>(bits >> (8 * byte));
            }
        }
    }
    std::vector<char> compressed(ZSTD_compressBound(bytes.size()));
    const auto size = ZSTD_compress(compressed.data(), compressed.size(), bytes.data(), bytes.size(), 1);
    ASSERT_FALSE(ZSTD_isError(size)) << ZSTD_getErrorName(size);
    {
        std::ofstream chunk(copy / "SKY" / "c" / "0" / "0" / "0" / "0" / "0", std::ios::binary | std::ios::trunc);
        chunk.write(compressed.data(), size);
        ASSERT_TRUE(chunk.good());
    }

    auto loader = FileLoader::GetLoader(copy.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("SKY");
    const casacore::Slicer plane(casacore::IPosition(4, 0, 0, 0, 0), casacore::IPosition(4, kWidth, kHeight, 1, 1));
    // Prove the source pixels are infinities, rather than pixels already suppressed by the flag.
    casacore::Array<float> raw;
    loader->GetImage()->getSlice(raw, plane);
    EXPECT_EQ(raw(casacore::IPosition(4, 0, 1, 0, 0)), std::numeric_limits<float>::infinity());
    EXPECT_EQ(raw(casacore::IPosition(4, 0, 2, 0, 0)), -std::numeric_limits<float>::infinity());

    casacore::Array<float> pixels(plane.length());
    ASSERT_TRUE(loader->GetSlice(pixels, StokesSlicer(StokesSource(), plane)));
    casacore::Array<bool> mask;
    loader->GetImage()->getMaskSlice(mask, plane);
    EXPECT_TRUE(allEQ(mask, isFinite(pixels)));
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const float value = pixels(casacore::IPosition(4, x, y, 0, 0));
            if (!ExpectedFlag(x, y) || (x == 0 && (y == 1 || y == 2)) || (x == 1 && y == 1)) {
                EXPECT_TRUE(std::isnan(value)) << "x=" << x << " y=" << y;
            } else {
                EXPECT_FLOAT_EQ(value, ExpectedValue(x, y, 0, 0)) << "x=" << x << " y=" << y;
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

// A direct loader read leaves a small mask available for a subsequent casacore consumer.
TEST_F(ZarrImageTest, ALoaderReadLeavesItsMaskAvailable) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    auto* image = dynamic_cast<CartaZarrImage*>(loader->GetImage().get());
    ASSERT_NE(image, nullptr);

    const casacore::Slicer cube(casacore::IPosition(4, 0, 0, 0, 0), casacore::IPosition(4, kWidth, kHeight, kDepth, 1));
    casacore::Array<float> data(cube.length());
    ASSERT_TRUE(loader->GetSlice(data, StokesSlicer(StokesSource(), cube)));
    EXPECT_EQ(image->MaskCacheBytes(), static_cast<std::size_t>(cube.length().product()));
    casacore::Array<bool> mask;
    image->getMaskSlice(mask, cube);
    EXPECT_TRUE(allEQ(mask, isFinite(data)));
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

// The same Frame path serves channel switching and animation frames. Revisit planes to catch stale caches.
TEST_F(ZarrImageTest, RasterTilesFollowChannelAndStokesSwitching) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    Frame frame(0, loader, "SKY");
    ASSERT_TRUE(frame.IsValid());
    EXPECT_EQ(frame.Depth(), kDepth);
    EXPECT_EQ(frame.NumStokes(), kStokes);

    for (int round = 0; round < 2; ++round) {
        for (int stokes = 0; stokes < kStokes; ++stokes) {
            for (int z = 0; z < kDepth; ++z) {
                std::string message;
                if (frame.CurrentZ() != z || frame.CurrentStokes() != stokes) {
                    ASSERT_TRUE(frame.SetImageChannels(z, stokes, message)) << message;
                }
                EXPECT_EQ(frame.CurrentZ(), z);
                EXPECT_EQ(frame.CurrentStokes(), stokes);
                for (bool current : {true, false}) {
                    const int requested_z = current ? z : 1 - z;
                    CARTA::RasterTileData response;
                    bool error = false;
                    ASSERT_TRUE(frame.FillRasterTileData(
                        response, Tile{0, 0, 0}, requested_z, stokes, CARTA::CompressionType::NONE, 0, current, error));
                    EXPECT_FALSE(error);
                    EXPECT_EQ(response.channel(), requested_z);
                    EXPECT_EQ(response.stokes(), stokes);
                    ASSERT_EQ(response.tiles_size(), 1);
                    const auto& tile = response.tiles(0);
                    ASSERT_EQ(tile.width(), kWidth);
                    ASSERT_EQ(tile.height(), kHeight);
                    ASSERT_EQ(tile.image_data().size(), kWidth * kHeight * sizeof(float));
                    std::vector<float> pixels(kWidth * kHeight);
                    std::memcpy(pixels.data(), tile.image_data().data(), tile.image_data().size());
                    for (int y = 0; y < kHeight; ++y) {
                        for (int x = 0; x < kWidth; ++x) {
                            const float value = pixels[y * kWidth + x];
                            if (InMissingChunk(x, requested_z, stokes) || !ExpectedFlag(x, y)) {
                                EXPECT_TRUE(std::isnan(value));
                            } else {
                                EXPECT_FLOAT_EQ(value, ExpectedValue(x, y, requested_z, stokes));
                            }
                        }
                    }
                }
            }
        }
    }
}

TEST_F(ZarrImageTest, ContoursUseTheSelectedChannel) {
    Frame frame(0, FileLoader::GetLoader(kZarrFixture.string()), "SKY");
    ASSERT_TRUE(frame.IsValid());
    for (int z = 0; z < kDepth; ++z) {
        std::string message;
        if (frame.CurrentZ() != z || frame.CurrentStokes() != 0) {
            ASSERT_TRUE(frame.SetImageChannels(z, 0, message)) << message;
        }
        CARTA::SetContourParameters settings;
        settings.mutable_image_bounds()->set_x_max(kWidth);
        settings.mutable_image_bounds()->set_y_max(kHeight);
        const double level = ExpectedValue(1, 2, z, 0);
        settings.add_levels(level);
        settings.set_smoothing_mode(CARTA::SmoothingMode::NoSmoothing);
        settings.set_decimation_factor(1);
        settings.set_contour_chunk_size(100000);
        ASSERT_TRUE(frame.SetContourParameters(settings));
        std::size_t vertex_count = 0;
        double completed = 0;
        ContourCallback callback = [&](double actual_level, double progress, const std::vector<float>& vertices,
                                       const std::vector<int>& indices) {
            EXPECT_DOUBLE_EQ(actual_level, level);
            completed = progress;
            vertex_count += vertices.size();
            for (float vertex : vertices) {
                EXPECT_TRUE(std::isfinite(vertex));
            }
        };
        ASSERT_TRUE(frame.ContourImage(callback, z));
        EXPECT_DOUBLE_EQ(completed, 1.0);
        EXPECT_GT(vertex_count, 0u);
    }
}

TEST_F(ZarrImageTest, VectorOverlayReadsQAndUOfTheSelectedChannel) {
    Frame frame(0, FileLoader::GetLoader(kZarrFixture.string()), "SKY");
    ASSERT_TRUE(frame.IsValid());
    CARTA::SetVectorOverlayParameters settings;
    settings.set_smoothing_factor(1);
    settings.set_stokes_intensity(1); // linear polarization intensity from Q and U
    settings.set_stokes_angle(1); // polarization angle from Q and U
    settings.set_threshold(std::numeric_limits<double>::quiet_NaN());
    settings.set_compression_type(CARTA::CompressionType::NONE);
    ASSERT_TRUE(frame.SetVectorOverlayParameters(settings));
    for (int z = 0; z < kDepth; ++z) {
        std::string message;
        if (frame.CurrentZ() != z || frame.CurrentStokes() != 0) {
            ASSERT_TRUE(frame.SetImageChannels(z, 0, message)) << message;
        }
        int responses = 0;
        ASSERT_TRUE(frame.CalculateVectorField([&](CARTA::VectorOverlayTileData& response) {
            ++responses;
            EXPECT_EQ(response.channel(), z);
            EXPECT_DOUBLE_EQ(response.progress(), 1.0);
            ASSERT_EQ(response.intensity_tiles_size(), 1);
            ASSERT_EQ(response.angle_tiles_size(), 1);
            const auto& intensity = response.intensity_tiles(0);
            const auto& angle = response.angle_tiles(0);
            ASSERT_EQ(intensity.image_data().size(), kWidth * kHeight * sizeof(float));
            ASSERT_EQ(angle.image_data().size(), kWidth * kHeight * sizeof(float));
            std::vector<float> pi(kWidth * kHeight), pa(kWidth * kHeight);
            std::memcpy(pi.data(), intensity.image_data().data(), intensity.image_data().size());
            std::memcpy(pa.data(), angle.image_data().data(), angle.image_data().size());
            for (int y = 0; y < kHeight; ++y) {
                for (int x = 0; x < kWidth; ++x) {
                    const int at = y * kWidth + x;
                    if (!ExpectedFlag(x, y) || InMissingChunk(x, z, 2)) {
                        EXPECT_TRUE(std::isnan(pi[at]));
                        EXPECT_TRUE(std::isnan(pa[at]));
                    } else {
                        const float q = ExpectedValue(x, y, z, 1);
                        const float u = ExpectedValue(x, y, z, 2);
                        EXPECT_NEAR(pi[at], std::hypot(q, u), 0.001f);
                        EXPECT_NEAR(pa[at], 0.5f * std::atan2(u, q) * 180.0f / M_PI, 0.001f);
                    }
                }
            }
        }));
        EXPECT_EQ(responses, 1);
    }
}
