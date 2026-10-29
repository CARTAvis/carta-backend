/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/


#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numeric>
#include <string>
#include <stdexcept>
#include <vector>

#include <zstd.h>

#include "CommonTestUtilities.h"
#include "Frame/Frame.h"
#include "ImageData/ZarrLoader.h"
#include "ImageData/ZarrStores.h"
#include "ImageStats/StatsCalculator.h"

using namespace carta;
namespace fs = std::filesystem;

namespace {

class BudgetedZarrLoader : public ZarrLoader {
public:
    using ZarrLoader::ZarrLoader;
    void LimitReads() {
        _histogram_read_budget_bytes = 1;
    }
};

class ZarrCubeHistogramTest : public ::testing::Test {
protected:
    const fs::path fixture{ZARR_PIXEL_FIXTURE};
    void TearDown() override {
        ZarrStores::Instance().Clear();
    }
    fs::path Copy(const std::string& name) {
        const auto copy = TestRoot() / "data" / "generated" / name;
        fs::remove_all(copy);
        fs::copy(fixture, copy, fs::copy_options::recursive);
        return copy;
    }
    fs::path Filled(const std::string& name, float value) {
        const auto copy = Copy(name);
        std::uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        std::array<unsigned char, 40> raw{};
        for (std::size_t i = 0; i < 10; ++i) {
            for (std::size_t byte = 0; byte < sizeof(float); ++byte) {
                raw[sizeof(float) * i + byte] = static_cast<unsigned char>(bits >> (byte * 8));
            }
        }
        std::vector<char> compressed(ZSTD_compressBound(raw.size()));
        const auto size = ZSTD_compress(compressed.data(), compressed.size(), raw.data(), raw.size(), 1);
        if (ZSTD_isError(size)) {
            throw std::runtime_error(ZSTD_getErrorName(size));
        }
        for (int z = 0; z < 2; ++z) {
            for (int stokes = 0; stokes < 3; ++stokes) {
                for (int x = 0; x < 2; ++x) {
                    const auto chunk = copy / "SKY" / "c" / "0" / std::to_string(z) / std::to_string(stokes) / std::to_string(x) / "0";
                    fs::create_directories(chunk.parent_path());
                    std::ofstream(chunk, std::ios::binary | std::ios::trunc).write(compressed.data(), size);
                }
            }
        }
        return copy;
    }
};

TEST_F(ZarrCubeHistogramTest, OnePassCountsAllPlanesAndStokesWithFlagsAndMissingChunks) {
    auto loader = std::make_shared<BudgetedZarrLoader>(fixture.string());
    loader->OpenFile("SKY");
    loader->LimitReads();
    for (int stokes = 0; stokes < 3; ++stokes) {
        const casacore::Slicer cube(casacore::IPosition(4, 0, 0, 0, stokes), casacore::IPosition(4, 4, 5, 2, 1));
        casacore::Array<float> data(cube.length());
        ASSERT_TRUE(loader->GetSlice(data, StokesSlicer(StokesSource(), cube)));
        BasicStats<float> expected;
        CalcBasicStats(expected, data.data(), data.nelements());
        const auto exact = CalcHistogram(4, HistogramBounds(expected.min_val, expected.max_val), data.data(), data.nelements());
        BasicStats<float> actual;
        Histogram histogram;
        double previous = 0;
        int reports = 0;
        auto report = [&](const CubeHistogramProgress& update) {
            EXPECT_GE(update.progress, previous);
            EXPECT_GE(update.progress, 0);
            EXPECT_LE(update.progress, 1);
            previous = update.progress;
            BasicStats<float> partial_stats;
            Histogram partial;
            update.snapshot(partial_stats, partial);
            EXPECT_LE(partial_stats.num_pixels, expected.num_pixels);
            EXPECT_EQ(std::accumulate(partial.GetHistogramBins().begin(), partial.GetHistogramBins().end(), 0), partial_stats.num_pixels);
            ++reports;
            return true;
        };
        ASSERT_EQ(loader->OnePassCubeHistogram(stokes, 4, actual, histogram, {}, report), CubeHistogramStatus::finished);
        EXPECT_GT(reports, 0);
        EXPECT_EQ(actual.num_pixels, expected.num_pixels);
        EXPECT_DOUBLE_EQ(actual.sum, expected.sum);
        EXPECT_DOUBLE_EQ(actual.mean, expected.mean);
        EXPECT_NEAR(actual.stdDev, expected.stdDev, 1e-8);
        EXPECT_FLOAT_EQ(actual.min_val, expected.min_val);
        EXPECT_FLOAT_EQ(actual.max_val, expected.max_val);
        EXPECT_EQ(histogram.GetHistogramBins(), exact.GetHistogramBins());
    }
}

TEST_F(ZarrCubeHistogramTest, FrameResolvesAutoBinsAndReusesTheCompletedCubeHistogram) {
    auto loader = FileLoader::GetLoader(fixture.string());
    Frame frame(0, loader, "SKY");
    ASSERT_TRUE(frame.IsValid());
    CARTA::HistogramConfig requirement;
    requirement.set_coordinate("z");
    requirement.set_channel(ALL_Z);
    requirement.set_num_bins(AUTO_BIN_SIZE);
    ASSERT_TRUE(frame.SetHistogramRequirements(CUBE_REGION_ID, {requirement}));
    int messages = 0;
    auto receive = [&](CARTA::RegionHistogramData message) {
        ++messages;
        EXPECT_EQ(message.channel(), ALL_Z);
        EXPECT_FLOAT_EQ(message.progress(), 1);
        EXPECT_EQ(message.histograms().num_bins(), 4);
        EXPECT_EQ(std::accumulate(message.histograms().bins().begin(), message.histograms().bins().end(), 0), 26);
    };
    EXPECT_FALSE(frame.FillRegionHistogramData(receive, CUBE_REGION_ID, 0, false));
    BasicStats<float> stats;
    Histogram histogram;
    ASSERT_EQ(frame.CalculateOnePassCubeHistogram(0, HistogramConfig(requirement), stats, histogram, {}, {}), CubeHistogramStatus::finished);
    EXPECT_TRUE(frame.FillRegionHistogramData(receive, CUBE_REGION_ID, 0, false));
    EXPECT_EQ(messages, 1);
}

TEST_F(ZarrCubeHistogramTest, FixedBoundsComputedStokesAndUnsupportedLoadersDeclineBeforeReading) {
    auto loader = FileLoader::GetLoader(fixture.string());
    Frame frame(0, loader, "SKY");
    HistogramConfig config;
    config.fixed_bounds = true;
    config.num_bins = 4;
    BasicStats<float> stats;
    stats.num_pixels = 42;
    Histogram histogram(2, HistogramBounds(-1, 1), nullptr, 0);
    EXPECT_EQ(frame.CalculateOnePassCubeHistogram(0, config, stats, histogram, {}, {}), CubeHistogramStatus::declined);
    config.fixed_bounds = false;
    EXPECT_EQ(frame.CalculateOnePassCubeHistogram(CARTA::PolarizationType::Plinear, config, stats, histogram, {}, {}), CubeHistogramStatus::declined);
    auto unsupported = FileLoader::GetLoader(frame.GetImage(), "memory");
    EXPECT_EQ(unsupported->OnePassCubeHistogram(0, 4, stats, histogram, {}, {}), CubeHistogramStatus::declined);
    EXPECT_EQ(stats.num_pixels, 42);
    EXPECT_EQ(histogram.GetNbins(), 2);
}

TEST_F(ZarrCubeHistogramTest, CancellationBeforeOrDuringAScanLeavesOutputsUntouched) {
    BudgetedZarrLoader loader(fixture.string());
    loader.OpenFile("SKY");
    loader.LimitReads();
    BasicStats<float> stats;
    stats.num_pixels = 42;
    Histogram histogram(2, HistogramBounds(-1, 1), nullptr, 0);
    EXPECT_EQ(loader.OnePassCubeHistogram(0, 4, stats, histogram, [] { return true; }, {}), CubeHistogramStatus::cancelled);
    int reports = 0;
    auto stop = [&](const CubeHistogramProgress&) { ++reports; return false; };
    EXPECT_EQ(loader.OnePassCubeHistogram(0, 4, stats, histogram, {}, stop), CubeHistogramStatus::cancelled);
    EXPECT_EQ(reports, 1);
    EXPECT_EQ(stats.num_pixels, 42);
    EXPECT_EQ(histogram.GetNbins(), 2);
}

TEST_F(ZarrCubeHistogramTest, AClosedFrameDoesNotStartACubeScan) {
    Frame frame(0, FileLoader::GetLoader(fixture.string()), "SKY");
    ASSERT_TRUE(frame.IsValid());
    frame.WaitForTaskCancellation();
    BasicStats<float> stats;
    stats.num_pixels = 42;
    Histogram histogram(2, HistogramBounds(-1, 1), nullptr, 0);
    EXPECT_EQ(frame.CalculateOnePassCubeHistogram(0, HistogramConfig(), stats, histogram, {}, {}), CubeHistogramStatus::cancelled);
    EXPECT_EQ(stats.num_pixels, 42);
    EXPECT_EQ(histogram.GetNbins(), 2);
}

TEST_F(ZarrCubeHistogramTest, ADecodeFailureDoesNotPublishACompletedHistogram) {
    const auto copy = Copy("cube_broken_chunk.zarr");
    const auto chunk = copy / "SKY" / "c" / "0" / "0" / "0" / "0" / "0";
    fs::resize_file(chunk, fs::file_size(chunk) / 2);
    ZarrLoader loader(copy.string());
    loader.OpenFile("SKY");
    BasicStats<float> stats;
    stats.num_pixels = 42;
    Histogram histogram(2, HistogramBounds(-1, 1), nullptr, 0);
    EXPECT_EQ(loader.OnePassCubeHistogram(0, 4, stats, histogram, {}, {}), CubeHistogramStatus::failed);
    EXPECT_EQ(stats.num_pixels, 42);
    EXPECT_EQ(histogram.GetNbins(), 2);
}

TEST_F(ZarrCubeHistogramTest, EmptyAndConstantCubesFollowExistingHistogramConventions) {
    for (float value : {0.0f, 5.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        const auto copy = Filled(std::string("cube_constant_") + std::to_string(value), value);
        ZarrLoader loader(copy.string());
        loader.OpenFile("SKY");
        BasicStats<float> stats;
        Histogram histogram;
        ASSERT_EQ(loader.OnePassCubeHistogram(0, 4, stats, histogram, {}, {}), CubeHistogramStatus::finished);
        EXPECT_EQ(stats.num_pixels, std::isfinite(value) ? 26 : 0);
        EXPECT_EQ(histogram.GetNbins(), 1);
        EXPECT_FLOAT_EQ(histogram.GetBinWidth(), 0);
        ASSERT_EQ(histogram.GetHistogramBins().size(), 1);
        EXPECT_EQ(histogram.GetHistogramBins()[0], value == 0 ? 26 : 0);
    }
}

} // namespace
