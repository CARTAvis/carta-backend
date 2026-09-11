/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include <casacore/casa/Arrays/Array.h>
#include <casacore/coordinates/Coordinates/ObsInfo.h>
#include <casacore/lattices/Lattices/MaskedLatticeIterator.h>
#include <casacore/measures/Measures/MPosition.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <vector>

#include <casacore/images/Images/SubImage.h>
#include <casacore/lattices/LRegions/LCBox.h>

#include "ImageData/CartaZarrImage.h"
#include "ImageData/FileLoader.h"
#include "ImageGenerators/ImageGenerator.h"
#include "ImageStats/StatsCalculator.h"
#include "Region/RegionHandler.h"
#include "Util/Message.h"
#include "src/Frame/Frame.h"

using namespace carta;

namespace {

const std::filesystem::path kZarrFixture{ZARR_PIXEL_FIXTURE};

constexpr int kWidth = 4;
constexpr int kHeight = 5;
constexpr int kDepth = 2;
constexpr int kStokes = 3;

float ExpectedValue(int x, int y, int z, int stokes) {
    return static_cast<float>((z * 1000) + (stokes * 100) + (x * 10) + y);
}

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
};

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

TEST_F(ZarrImageTest, GetSliceReturnsCorrectPixels) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");

    for (int stokes = 0; stokes < kStokes; ++stokes) {
        for (int z = 0; z < kDepth; ++z) {
            casacore::Slicer slicer(casacore::IPosition(4, 0, 0, z, stokes),
                casacore::IPosition(4, kWidth, kHeight, 1, 1));
            casacore::Array<float> data(slicer.length());
            ASSERT_TRUE(loader->GetSlice(data, StokesSlicer(StokesSource(), slicer)))
                << "GetSlice failed at z=" << z << " stokes=" << stokes;
            ASSERT_EQ(data.nelements(), kWidth * kHeight);

            for (int y = 0; y < kHeight; ++y) {
                for (int x = 0; x < kWidth; ++x) {
                    const float value = data(casacore::IPosition(4, x, y, 0, 0));
                    if (InMissingChunk(x, z, stokes)) {
                        EXPECT_TRUE(std::isnan(value)) << "x=" << x << " y=" << y << " z=" << z;
                    } else if (!ExpectedFlag(x, y)) {
                        EXPECT_TRUE(std::isnan(value)) << "flagged pixel x=" << x << " y=" << y;
                    } else {
                        EXPECT_FLOAT_EQ(value, ExpectedValue(x, y, z, stokes))
                            << "x=" << x << " y=" << y << " z=" << z << " stokes=" << stokes;
                    }
                }
            }
        }
    }
}

TEST_F(ZarrImageTest, GetSliceHonoursASubRegion) {
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
            const int image_x = x + 1;
            const int image_y = y + 2;
            if (!ExpectedFlag(image_x, image_y)) {
                EXPECT_TRUE(std::isnan(value));
            } else {
                EXPECT_FLOAT_EQ(value, ExpectedValue(image_x, image_y, 0, 1));
            }
        }
    }
}

// The mask casacore sees is the finiteness of the pixels, which is how a flagged or absent pixel
// reaches every casacore consumer that has no NaN handling of its own.
TEST_F(ZarrImageTest, MaskMarksFlaggedPixels) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    auto image = loader->GetImage();
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

TEST_F(ZarrImageTest, TelescopePositionIsCartesian) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    auto image = loader->GetImage();
    ASSERT_NE(image, nullptr);

    const auto position = image->coordinates().obsInfo().telescopePosition();
    const auto value = position.getValue().getValue();  // metres, x y z
    ASSERT_EQ(value.size(), 3);
    // longitude 2.0 rad, latitude -0.5 rad, radius 6371000 m
    EXPECT_NEAR(value(0), -2326709.631, 1.0);
    EXPECT_NEAR(value(1), 5083953.295, 1.0);
    EXPECT_NEAR(value(2), -3054420.106, 1.0);
}

// Statistics over one plane of the fixture. Seven of the twenty pixels are flagged, so a correct
// reader reports thirteen points summing to 217. carta-zarr writes NaN for those seven, and
// casacore's ImageStatistics has no NaN handling of its own: it excludes a pixel only when the
// image reports a mask.
TEST_F(ZarrImageTest, PlaneStatisticsExcludeFlaggedPixels) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    auto image = loader->GetImage();
    ASSERT_NE(image, nullptr);

    const casacore::Slicer plane(
        casacore::IPosition(4, 0, 0, 0, 0), casacore::IPosition(4, kWidth, kHeight, 1, 1));
    casacore::SubImage<float> sub_image(*image, casacore::LCBox(plane, image->shape()), false);

    const std::vector<CARTA::StatsType> requested{
        CARTA::StatsType::NumPixels, CARTA::StatsType::Sum, CARTA::StatsType::Mean,
        CARTA::StatsType::Min, CARTA::StatsType::Max};
    std::map<CARTA::StatsType, std::vector<double>> stats;
    ASSERT_TRUE(CalcStatsValues(stats, requested, sub_image, false));

    EXPECT_DOUBLE_EQ(stats[CARTA::StatsType::NumPixels][0], 13.0);
    EXPECT_DOUBLE_EQ(stats[CARTA::StatsType::Sum][0], 217.0);
    EXPECT_NEAR(stats[CARTA::StatsType::Mean][0], 217.0 / 13.0, 1e-9);
    EXPECT_DOUBLE_EQ(stats[CARTA::StatsType::Min][0], 1.0);
    EXPECT_DOUBLE_EQ(stats[CARTA::StatsType::Max][0], 34.0);
}

// The mask is cached by the pixel read, so it must agree whether or not a read preceded it, and
// the cursor casacore iterates with must be the chunk rather than a whole row.
TEST_F(ZarrImageTest, CachedMaskAgreesWithADirectMaskRead) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    auto image = loader->GetImage();
    ASSERT_NE(image, nullptr);

    const casacore::Slicer slicer(
        casacore::IPosition(4, 0, 0, 1, 2), casacore::IPosition(4, kWidth, kHeight, 1, 1));

    casacore::Array<bool> direct(slicer.length());
    image->getMaskSlice(direct, slicer);  // no preceding read: the fallback path

    casacore::Array<float> pixels(slicer.length());
    image->getSlice(pixels, slicer);
    casacore::Array<bool> cached(slicer.length());
    image->getMaskSlice(cached, slicer);  // same section: the cached path

    EXPECT_TRUE(allEQ(direct, cached));
    // This plane holds the missing chunk, whose pixels are not flagged but are absent.
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const bool expected = ExpectedFlag(x, y) && !InMissingChunk(x, 1, 2);
            EXPECT_EQ(cached(casacore::IPosition(4, x, y, 0, 0)), expected) << "x=" << x << " y=" << y;
        }
    }
}

// The batched reduction is what a position-velocity cut uses instead of asking for one box at a
// time. Its answers have to be the ones a per-region read would give, including for the flagged
// pixels and the chunk the fixture never wrote.
TEST_F(ZarrImageTest, MultiRegionSpectralDataMatchesAPerRegionSum) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");

    // A mask selecting the two pixels on one diagonal of a 2 x 2 box, laid out x fastest, exactly
    // as casacore's LCRegionFixed stores one.
    const casacore::Bool diagonal[]{true, false, false, true};

    std::vector<RegionMaskSpec> regions{
        {0, 0, kWidth, kHeight, nullptr},  // the whole plane
        {2, 0, 2, kHeight, nullptr},       // the right chunk, which is missing at z = 1, stokes = 2
        {1, 1, 2, 2, diagonal},     // a raster mask inside its bounding box
    };

    const auto expected = [&](const RegionMaskSpec& region, int z, int stokes, double& sum) {
        double count = 0.0;
        sum = 0.0;
        for (std::uint64_t y = region.y_start; y < region.y_start + region.height; ++y) {
            for (std::uint64_t x = region.x_start; x < region.x_start + region.width; ++x) {
                if (region.mask != nullptr &&
                    !region.mask[((y - region.y_start) * region.width) + (x - region.x_start)]) {
                    continue;
                }
                if (!ExpectedFlag(x, y) || InMissingChunk(x, z, stokes)) {
                    continue;
                }
                count += 1.0;
                sum += ExpectedValue(x, y, z, stokes);
            }
        }
        return count;
    };

    for (int stokes = 0; stokes < kStokes; ++stokes) {
        std::size_t channels_seen = 0;
        const bool reduced = loader->GetMultiRegionSpectralData(
            regions, AxisRange(0, kDepth - 1), stokes, [&](const RegionSpectralBlock& block) {
                EXPECT_EQ(block.region_count, regions.size());
                for (std::size_t r = 0; r < block.region_count; ++r) {
                    for (std::size_t c = 0; c < block.channel_count; ++c) {
                        const auto z = static_cast<int>(block.first_channel + c);
                        double sum = 0.0;
                        const double count = expected(regions[r], z, stokes, sum);
                        EXPECT_DOUBLE_EQ(block.num_pixels[(r * block.region_stride) + c], count)
                            << "region " << r << " z=" << z << " stokes=" << stokes;
                        EXPECT_DOUBLE_EQ(block.sum[(r * block.region_stride) + c], sum)
                            << "region " << r << " z=" << z << " stokes=" << stokes;
                    }
                }
                channels_seen += block.channel_count;
                return true;
            });
        ASSERT_TRUE(reduced) << "the batched reduction failed at stokes=" << stokes;
        EXPECT_EQ(channels_seen, static_cast<std::size_t>(kDepth));
    }
}

// The right chunk of the fixture is missing at z = 1, stokes = 2, so every one of its pixels is
// absent and the mean the PV generator would publish for that box is not a number.
TEST_F(ZarrImageTest, MultiRegionSpectralDataReportsAnEmptyChannel) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");

    const std::vector<RegionMaskSpec> regions{{2, 0, 2, kHeight, nullptr}};
    std::vector<double> counts(kDepth, -1.0);
    ASSERT_TRUE(loader->GetMultiRegionSpectralData(regions, AxisRange(0, kDepth - 1), 2,
        [&](const RegionSpectralBlock& block) {
            for (std::size_t c = 0; c < block.channel_count; ++c) {
                counts.at(block.first_channel + c) = block.num_pixels[c];
            }
            return true;
        }));
    EXPECT_GT(counts.at(0), 0.0);
    EXPECT_DOUBLE_EQ(counts.at(1), 0.0);
}

// A sink that stops has to stop the reduction rather than be called again.
TEST_F(ZarrImageTest, MultiRegionSpectralDataStopsWhenTheSinkDoes) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");

    const std::vector<RegionMaskSpec> regions{{0, 0, kWidth, kHeight, nullptr}};
    int blocks = 0;
    EXPECT_FALSE(loader->GetMultiRegionSpectralData(regions, AxisRange(0, kDepth - 1), 0,
        [&](const RegionSpectralBlock&) {
            ++blocks;
            return false;
        }));
    EXPECT_EQ(blocks, 1);
}

// The batched path and the per-box path have to be the same answer, not just each plausible on its
// own. Both frames below read the same Zarr file: the first through ZarrLoader, which reduces every
// box at once, and the second through a loader holding the same CartaZarrImage, which has no
// batched path and so walks the boxes one at a time.
TEST_F(ZarrImageTest, PvImageAgreesBetweenTheBatchedAndPerBoxPaths) {
    const auto pv_data = [&](const std::shared_ptr<FileLoader>& loader, bool& succeeded) {
        std::shared_ptr<Frame> frame(new Frame(0, loader, ""));
        carta::RegionHandler region_handler;
        const int file_id = 0;
        int region_id = -1;

        std::vector<CARTA::Point> control_points{Message::Point(0.0, 0.0), Message::Point(3.0, 4.0)};
        RegionState region_state(file_id, CARTA::RegionType::LINE, control_points, 0.0);
        region_handler.SetRegion(region_id, region_state, frame->CoordinateSystem());

        CARTA::PvRequest request;
        request.set_file_id(file_id);
        request.set_region_id(region_id);
        request.set_width(3);
        std::function<void(float)> progress_callback = [](float) {};
        CARTA::PvResponse response;
        GeneratedImage pv_image;
        region_handler.CalculatePvImage(request, frame, progress_callback, response, pv_image);

        casacore::Array<float> data;
        succeeded = response.success() && pv_image.image != nullptr;
        if (succeeded) {
            pv_image.image->get(data);
        }
        return data;
    };

    auto zarr_loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(zarr_loader, nullptr);
    bool batched_ok = false;
    const auto batched = pv_data(zarr_loader, batched_ok);
    ASSERT_TRUE(batched_ok) << "the PV image could not be generated through ZarrLoader";

    auto image = std::make_shared<CartaZarrImage>(kZarrFixture.string());
    auto image_loader = FileLoader::GetLoader(image, kZarrFixture.string());
    ASSERT_NE(image_loader, nullptr);
    bool per_box_ok = false;
    const auto per_box = pv_data(image_loader, per_box_ok);
    ASSERT_TRUE(per_box_ok) << "the PV image could not be generated one box at a time";

    ASSERT_EQ(batched.shape(), per_box.shape());
    auto batched_it = batched.begin();
    auto per_box_it = per_box.begin();
    std::size_t finite = 0;
    for (; batched_it != batched.end(); ++batched_it, ++per_box_it) {
        if (std::isnan(*per_box_it)) {
            EXPECT_TRUE(std::isnan(*batched_it));
        } else {
            EXPECT_FLOAT_EQ(*batched_it, *per_box_it);
            ++finite;
        }
    }
    // A comparison of two all-NaN images would pass without either path having read anything.
    EXPECT_GT(finite, 0u);
}

TEST_F(ZarrImageTest, NiceCursorShapeIsTheChunk) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    auto image = loader->GetImage();
    ASSERT_NE(image, nullptr);

    const auto cursor = image->niceCursorShape(image->advisedMaxPixels());
    ASSERT_EQ(cursor.size(), 4);
    // The fixture is chunked (l, m) = (2, 5) with one plane per chunk. The cursor is a whole
    // number of chunks along each spatial axis, grown until the advice stops it - here the whole
    // 4 x 5 plane - rather than casacore's default, which fills l and stops at one row.
    EXPECT_EQ(cursor(0), kWidth);
    EXPECT_EQ(cursor(1), kHeight);
    EXPECT_EQ(cursor(2), 1);
    EXPECT_EQ(cursor(3), 1);
    EXPECT_LE(cursor.product(), static_cast<casacore::Int>(image->advisedMaxPixels()));
}

}  // namespace