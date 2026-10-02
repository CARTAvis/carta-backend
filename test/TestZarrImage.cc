/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include <casacore/casa/Arrays/Array.h>
#include <casacore/coordinates/Coordinates/ObsInfo.h>
#include <casacore/lattices/Lattices/MaskedLatticeIterator.h>
#include <casacore/measures/Measures/MPosition.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include <casacore/images/Images/SubImage.h>
#include <casacore/lattices/LRegions/LCBox.h>
#include <casacore/lattices/LRegions/LCPixelSet.h>

#include "ImageData/CartaZarrImage.h"
#include "ImageData/FileLoader.h"
#include "ImageData/ZarrContext.h"
#include "Main/ProgramSettings.h"
#include "ImageData/ZarrLoader.h"
#include "Cache/FileInfoCache.h"
#include "FileList/FileInfoLoader.h"
#include "ImageGenerators/ImageGenerator.h"
#include "ImageStats/StatsCalculator.h"
#include "Region/RegionAnalysis/LineBoxRegions.h"
#include "Region/RegionAnalysis/RegionProfiles.h"
#include "Region/RegionHandler.h"
#include "Util/Message.h"
#include "src/Frame/Frame.h"

#include "CommonTestUtilities.h"

using namespace carta;

namespace {

const std::filesystem::path kZarrFixture{ZARR_PIXEL_FIXTURE};

// A Zarr loader whose walk over regions does as a test says: walk as the real one does, decline before
// starting, fail once the first block has been handed over -- which no store on disk can be made to do
// on cue, and which is the case a caller must not answer by reading box by box -- or have no walks at
// all, as every other loader has none. With none it has no walk over the cube either, which is how a
// test reaches the planes one at a time. How the cube's routes decline, fail and are stopped is
// CubeHistogramCalculator's, and is tested there.
class ScriptedZarrLoader : public carta::ZarrLoader {
public:
    enum class Script { walk, decline, fail_after_first_plane, no_walks };

    ScriptedZarrLoader(const std::string& filename, Script script) : ZarrLoader(filename), _script(script) {}

    CubeReducer* CubeWalk() override {
        return _script == Script::no_walks ? nullptr : this;
    }

    RegionReducer* RegionWalk() override {
        return _script == Script::no_walks ? nullptr : this;
    }

    BatchOutcome RegionSpectra(const std::vector<RegionMaskSpec>& regions, const AxisRange& z_range, int stokes,
        const std::function<bool(const RegionSpectralBlock&)>& sink) override {
        ++region_walks;
        if (_script == Script::decline) {
            return last_region_walk = BatchOutcome::declined;
        }
        if (_script == Script::fail_after_first_plane) {
            ZarrLoader::RegionSpectra(regions, z_range, stokes, [&](const RegionSpectralBlock& block) {
                sink(block);
                return false;
            });
            return last_region_walk = BatchOutcome::failed;
        }
        return last_region_walk = ZarrLoader::RegionSpectra(regions, z_range, stokes, sink);
    }

    // How often a walk over regions was asked for, and how the last one ended.
    int region_walks = 0;
    BatchOutcome last_region_walk = BatchOutcome::declined;

private:
    Script _script;
};

// A Zarr loader that counts the times it is asked to make a cube histogram in one pass.
class OnePassCountingLoader : public carta::ZarrLoader {
public:
    using carta::ZarrLoader::ZarrLoader;

    int one_pass_calls = 0;

    BatchOutcome OnePassCubeHistogram(int stokes, int num_bins, std::uint64_t spatial_sample, BasicStats<float>& stats,
        std::vector<int>& bins, const std::function<bool(const CubeHistogramUpdate&)>& progress) override {
        ++one_pass_calls;
        return ZarrLoader::OnePassCubeHistogram(stokes, num_bins, spatial_sample, stats, bins, progress);
    }
};

// A frame over the pixel fixture whose loader walks as `script` says.
std::shared_ptr<Frame> ScriptedFrame(ScriptedZarrLoader::Script script, const std::filesystem::path& path = kZarrFixture) {
    auto loader = std::make_shared<ScriptedZarrLoader>(path.string(), script);
    loader->OpenFile("");
    return std::make_shared<Frame>(0, loader, "");
}

HistogramConfig CubeConfig(int num_bins) {
    HistogramConfig config;
    config.channel = ALL_Z;
    config.num_bins = num_bins;
    return config;
}

// RegionHandler keeps its line profiles to itself, and the frames it works over too.
class PeekableRegionHandler : public carta::RegionHandler {
public:
    using carta::RegionHandler::_frames;
    using carta::RegionHandler::_line_profile_progress_interval;
    using carta::RegionHandler::_region_profiles;
    using carta::RegionHandler::GetLineProfiles;
};

// Frame keeps each plane's statistics to itself, and what a cube histogram leaves of them is a promise
// a request for one plane relies on.
class PeekableFrame : public carta::Frame {
public:
    using carta::Frame::CacheKey;
    using carta::Frame::Frame;
    using carta::Frame::_image_basic_stats;
};

// Bytes this process has been handed by read(), page-cache hits included: what the walk asked the
// filesystem for, as opposed to what reached the disk.
struct PlaneIo {
    long long rchar = 0;      // what read() handed over, page-cache hits included
    long long read_bytes = 0; // what actually came off the block device
    long long syscr = 0;      // how many read() calls that took
};

PlaneIo ReadIo() {
    PlaneIo counters;
    std::ifstream io("/proc/self/io");
    std::string key;
    long long value = 0;
    while (io >> key >> value) {
        if (key == "rchar:") {
            counters.rchar = value;
        } else if (key == "read_bytes:") {
            counters.read_bytes = value;
        } else if (key == "syscr:") {
            counters.syscr = value;
        }
    }
    return counters;
}

constexpr int kWidth = 4;
constexpr int kHeight = 5;
constexpr int kDepth = 2;
constexpr int kStokes = 3;

using ProfilesMap = std::map<CARTA::StatsType, std::vector<double>>;

// One step of a region's profile through the loader's own reading of it, of at most `step`.
BatchOutcome ProfileStep(RegionProfiles& profiles, FileLoader& loader, int region_id, int stokes,
    const casacore::ArrayLattice<casacore::Bool>& mask, const casacore::IPosition& origin, ProfilesMap& profile, float& progress,
    const RegionProfileReport& report = {}, std::chrono::milliseconds step = std::chrono::milliseconds(TARGET_PARTIAL_REGION_TIME)) {
    static std::mutex image_mutex;
    RegionProfileRequest request;
    request.origin = origin;
    request.mask = &mask;
    request.channels = AxisRange(0, kDepth - 1);
    request.stokes = stokes;
    auto* reader = loader.ProfileReader();
    if (!reader) {
        return BatchOutcome::declined;
    }
    return profiles.Continue({0, region_id, stokes}, request, *reader, image_mutex, step, report, profile, progress);
}

// A region's profile made to the end, over every channel of the fixture.
bool WholeProfile(RegionProfiles& profiles, FileLoader& loader, int region_id, int stokes,
    const casacore::ArrayLattice<casacore::Bool>& mask, const casacore::IPosition& origin, ProfilesMap& profile,
    const RegionProfileReport& report = {}) {
    float progress = 0.0f;
    for (int steps = 0; progress < 1.0f; ++steps) {
        if (steps >= 200 ||
            ProfileStep(profiles, loader, region_id, stokes, mask, origin, profile, progress, report) != BatchOutcome::finished) {
            return false;
        }
    }
    return true;
}

float ExpectedValue(int x, int y, int z, int stokes) {
    return static_cast<float>((z * 1000) + (stokes * 100) + (x * 10) + y);
}

bool InMissingChunk(int x, int z, int stokes) {
    return z == 1 && stokes == 2 && x >= 2;
}

bool ExpectedFlag(int x, int y) {
    return ((x + y) % 3) != 0;
}

// Pixels of a region mask that the image's own flags also keep, which is what a profile counts.
std::size_t CountUnflagged(const casacore::Array<casacore::Bool>& mask) {
    std::size_t count = 0;
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            if (mask(casacore::IPosition(2, x, y)) && ExpectedFlag(x, y)) {
                ++count;
            }
        }
    }
    return count;
}

class ZarrImageTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!std::filesystem::exists(kZarrFixture)) {
            GTEST_SKIP() << "carta-zarr pixel fixture not found at " << kZarrFixture;
        }
    }
};

namespace {

// A copy of the pixel fixture that holds the pixels a test puts in it and no others.
//
// The copy's sky is stored raw rather than compressed, so that a test can write it without a codec.
// A chunk nothing was put in is absent and reads as NaN, as the fill value says, so a plane is empty
// until a test puts something in it. The flags are the fixture's own: a pixel ExpectedFlag does not
// keep reads as NaN whatever is put there, and putting one is a mistake in the test.
class WrittenZarrFixture {
public:
    explicit WrittenZarrFixture(const std::string& name) : _path(TestRoot() / "data" / "generated" / name) {}

    void Put(int x, int y, int z, int stokes, float value) {
        if (!ExpectedFlag(x, y)) {
            ADD_FAILURE() << "the fixture flags x=" << x << " y=" << y << ", so nothing put there is read";
            return;
        }
        auto& chunk = _chunks[{z, stokes, x / 2}];
        if (chunk.empty()) {
            chunk.assign(2 * kHeight, std::numeric_limits<float>::quiet_NaN());
        }
        // Two columns to a chunk, the first column's rows first.
        chunk[(x % 2) * kHeight + y] = value;
    }

    // Writes the copy with what has been put in it, and says where it is.
    std::filesystem::path Write() const {
        std::filesystem::remove_all(_path);
        std::filesystem::copy(kZarrFixture, _path, std::filesystem::copy_options::recursive);
        std::filesystem::remove_all(_path / "SKY" / "c");

        const auto metadata = _path / "SKY" / "zarr.json";
        std::stringstream text;
        text << std::ifstream(metadata).rdbuf();
        const std::regex codecs(R"("codecs"\s*:\s*\[[^\]]*\])");
        std::ofstream(metadata, std::ios::trunc)
            << std::regex_replace(text.str(), codecs, R"("codecs": [{"name": "bytes", "configuration": {"endian": "little"}}])");

        for (const auto& [key, pixels] : _chunks) {
            const auto& [z, stokes, column_pair] = key;
            const auto chunk = _path / "SKY" / "c" / "0" / std::to_string(z) / std::to_string(stokes) / std::to_string(column_pair) / "0";
            std::filesystem::create_directories(chunk.parent_path());
            std::ofstream(chunk, std::ios::binary)
                .write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size() * sizeof(float)));
        }
        return _path;
    }

private:
    std::filesystem::path _path;
    // By plane, stokes and pair of columns.
    std::map<std::tuple<int, int, int>, std::vector<float>> _chunks;
};

// Every plane's statistics from the loader's walk, against the loop the caller would otherwise run:
// read the plane, then reduce it. The comparison is the point -- the walk exists to avoid
// materialising a plane and to avoid reading each one twice, and neither is worth anything if it
// disagrees.
void ExpectPlaneStatsAgreeWithThePerPlaneLoop(const std::filesystem::path& path) {
    auto loader = FileLoader::GetLoader(path.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    auto image = loader->GetImage();
    ASSERT_NE(image, nullptr);

    for (int stokes = 0; stokes < kStokes; ++stokes) {
        std::map<int, BasicStats<float>> from_loader;
        ASSERT_EQ(loader->CubeWalk()->PlaneStats(stokes, {},
                      [&](int z, const BasicStats<float>& stats) {
                          EXPECT_EQ(from_loader.count(z), 0u) << "plane " << z << " was reported twice";
                          from_loader[z] = stats;
                          return true;
                      }),
            BatchOutcome::finished)
            << "the Zarr loader should serve cube statistics itself";
        ASSERT_EQ(from_loader.size(), static_cast<std::size_t>(kDepth));

        for (int z = 0; z < kDepth; ++z) {
            casacore::Slicer slicer(casacore::IPosition(4, 0, 0, z, stokes), casacore::IPosition(4, kWidth, kHeight, 1, 1));
            // doGetSlice's Bool says whether the array references the lattice, not whether it
            // succeeded, so it is the shape that is checked here.
            casacore::Array<float> plane;
            image->doGetSlice(plane, slicer);
            ASSERT_EQ(plane.nelements(), static_cast<std::size_t>(kWidth) * kHeight);
            BasicStats<float> reference;
            CalcBasicStats(reference, plane.data(), plane.nelements());

            const std::string where = " z=" + std::to_string(z) + " stokes=" + std::to_string(stokes);
            const auto& actual = from_loader[z];
            EXPECT_EQ(actual.num_pixels, reference.num_pixels) << where;
            EXPECT_FLOAT_EQ(actual.min_val, reference.min_val) << where;
            EXPECT_FLOAT_EQ(actual.max_val, reference.max_val) << where;
            for (const auto& [name, pair] : std::map<std::string, std::pair<double, double>>{{"sum", {actual.sum, reference.sum}},
                     {"sumSq", {actual.sumSq, reference.sumSq}}, {"mean", {actual.mean, reference.mean}},
                     {"rms", {actual.rms, reference.rms}}, {"stdDev", {actual.stdDev, reference.stdDev}}}) {
                if (std::isnan(pair.second)) {
                    EXPECT_TRUE(std::isnan(pair.first)) << name << where << " is " << pair.first;
                } else {
                    EXPECT_NEAR(pair.first, pair.second, 1e-9 * (1.0 + std::abs(pair.second))) << name << where;
                }
            }
        }
    }
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
        const bool reduced =
            loader->RegionWalk()->RegionSpectra(regions, AxisRange(0, kDepth - 1), stokes, [&](const RegionSpectralBlock& block) {
                EXPECT_EQ(block.region_count, regions.size());
                for (std::size_t r = 0; r < block.region_count; ++r) {
                    for (std::size_t c = 0; c < block.channel_count; ++c) {
                        const auto z = static_cast<int>(block.first_channel + c);
                        double sum = 0.0;
                        const double count = expected(regions[r], z, stokes, sum);
                        EXPECT_DOUBLE_EQ(block.NumPixels(r)[c], count)
                            << "region " << r << " z=" << z << " stokes=" << stokes;
                        EXPECT_DOUBLE_EQ(block.Sum(r)[c], sum)
                            << "region " << r << " z=" << z << " stokes=" << stokes;
                    }
                }
                channels_seen += block.channel_count;
                return true;
            }) == BatchOutcome::finished;
        ASSERT_TRUE(reduced) << "the batched reduction failed at stokes=" << stokes;
        EXPECT_EQ(channels_seen, static_cast<std::size_t>(kDepth));
    }
}

// A reduction whose block takes more than one read hands that block over as it fills, and the
// arrivals are indistinguishable unless the flag comes with them. It did not: this path was the
// only batched one in the loader that neither asked for a read budget nor forwarded `complete`.
TEST_F(ZarrImageTest, MultiRegionSpectralDataMarksPartialBlocks) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    auto* zarr_loader = dynamic_cast<ZarrLoader*>(loader.get());
    ASSERT_NE(zarr_loader, nullptr);
    // One byte, so every chunk is its own read and the block cannot arrive finished the first time.
    zarr_loader->SetReadBudgetBytes(1);

    const std::vector<RegionMaskSpec> regions{{0, 0, kWidth, kHeight, nullptr}};
    std::size_t partials = 0;
    std::size_t complete_channels = 0;
    std::size_t every_arrival = 0;
    ASSERT_EQ(loader->RegionWalk()->RegionSpectra(regions, AxisRange(0, kDepth - 1), 0,
                  [&](const RegionSpectralBlock& block) {
                      every_arrival += block.channel_count;
                      if (block.complete) {
                          complete_channels += block.channel_count;
                          EXPECT_DOUBLE_EQ(block.completeness, 1.0);
                      } else {
                          ++partials;
                          EXPECT_GT(block.completeness, 0.0) << "a partial has read something";
                          EXPECT_LT(block.completeness, 1.0) << "and not everything";
                      }
                      return true;
                  }),
        BatchOutcome::finished);

    EXPECT_GT(partials, 0u) << "a one-byte budget should have split the block across reads";
    EXPECT_GT(every_arrival, complete_channels) << "the partials are exactly what a counter must not count";
    EXPECT_EQ(complete_channels, static_cast<std::size_t>(kDepth))
        << "counting only finished blocks should account for every channel exactly once";
}

// The consumer of the path above, which had no test at all -- which is how the bug survived: the
// guard rejected the batched pass, the caller fell back to reading each box on its own, and the
// answer came out right. The only symptom was that the optimisation never ran.
TEST_F(ZarrImageTest, BatchedLineProfilesSurviveASplitReduction) {
    auto loader = std::make_shared<ScriptedZarrLoader>(kZarrFixture.string(), ScriptedZarrLoader::Script::walk);
    loader->OpenFile("");
    loader->SetReadBudgetBytes(1);
    std::shared_ptr<Frame> frame(new Frame(0, loader, ""));
    ASSERT_TRUE(frame->IsValid());

    PeekableRegionHandler handler;
    const int file_id = 0;
    handler._frames[file_id] = frame;
    // Report on every arrival rather than twice a second, so the partials are visible at all.
    handler._line_profile_progress_interval = std::chrono::milliseconds(0);

    auto csys = frame->CoordinateSystem();
    std::vector<CARTA::Point> control_points{
        Message::Point(0.0, 0.0), Message::Point(kWidth - 1.0, kHeight - 1.0)};
    RegionState line_state(file_id, CARTA::RegionType::LINE, control_points, 0.0);
    int region_id = -1;
    ASSERT_TRUE(handler.SetRegion(region_id, line_state, csys));

    // A finished block moves progress by at least one whole channel, so any smaller forward step
    // came from a block reported while it was still filling. Merely seeing a fraction proves
    // nothing: an intermediate finished block gives one of those too.
    const float one_channel = 1.0f / static_cast<float>(kDepth);
    float highest_progress = -1.0f;
    float previous_progress = 0.0f;
    bool saw_step_finer_than_a_channel = false;
    std::function<void(float)> progress_callback = [&](float progress) {
        const float step = progress - previous_progress;
        if (step > 0.0f && step < one_channel - 1e-4f) {
            saw_step_finer_than_a_channel = true;
        }
        previous_progress = progress;
        highest_progress = std::max(highest_progress, progress);
    };
    casacore::Matrix<float> profiles;
    casacore::Quantity increment;
    bool cancelled = false;
    std::string message;
    EXPECT_TRUE(handler.GetLineProfiles(
        file_id, region_id, 3, AxisRange(0, kDepth - 1), 0, "", progress_callback, profiles, increment, cancelled, message, false))
        << message;
    // The boxes one at a time move progress by a box, which can be finer than a channel too, so
    // the walk is asked whether it was the one that answered.
    EXPECT_EQ(loader->region_walks, 1);
    EXPECT_EQ(loader->last_region_walk, BatchOutcome::finished) << "a reduction split across reads should still finish";
    EXPECT_FALSE(cancelled);
    ASSERT_EQ(profiles.shape().size(), 2u);
    EXPECT_GT(profiles.shape()(0), 0);
    EXPECT_EQ(profiles.shape()(1), kDepth);
    EXPECT_FLOAT_EQ(highest_progress, 1.0f) << "progress should reach one, and counting partials drove it past";
    EXPECT_TRUE(saw_step_finer_than_a_channel)
        << "without a block that arrives in pieces this test proves nothing, so the split is asserted too";
}

// The regions of a batched reduction are described only for a loader that has a walk to hand them
// to, because describing them means a mask per box and a declined caller builds its own again. A
// description that gives up declines just as a loader without a walk does, and reads nothing.
TEST_F(ZarrImageTest, RegionSpectraDescribesTheRegionsOnlyForALoaderWithAWalk) {
    const AxisRange channels(0, kDepth - 1);
    int blocks = 0;
    auto count_blocks = [&](const RegionSpectralBlock&) {
        ++blocks;
        return true;
    };

    auto without = ScriptedFrame(ScriptedZarrLoader::Script::no_walks);
    bool described = false;
    EXPECT_EQ(without->RegionSpectra(
                  [&](std::vector<RegionMaskSpec>&) {
                      described = true;
                      return true;
                  },
                  channels, 0, count_blocks),
        BatchOutcome::declined);
    EXPECT_FALSE(described) << "a loader without a walk should not cost the caller its masks";

    auto with = ScriptedFrame(ScriptedZarrLoader::Script::walk);
    EXPECT_EQ(with->RegionSpectra([](std::vector<RegionMaskSpec>&) { return false; }, channels, 0, count_blocks), BatchOutcome::declined);
    EXPECT_EQ(blocks, 0) << "a description that gave up should not have been walked";

    EXPECT_EQ(with->RegionSpectra(
                  [](std::vector<RegionMaskSpec>& regions) {
                      regions.push_back({0, 0, kWidth, kHeight, nullptr});
                      return true;
                  },
                  channels, 0, count_blocks),
        BatchOutcome::finished);
    EXPECT_GT(blocks, 0);
}

// A position-velocity image made through the handler's public request, over a frame whose loader
// walks as `script` says. The image's pixels, or nothing if it was not made.
std::vector<float> PvImagePixels(ScriptedZarrLoader::Script script, CARTA::PvResponse& response) {
    auto frame = ScriptedFrame(script);
    carta::RegionHandler handler;
    const int file_id = 0;
    int region_id = -1;
    std::vector<CARTA::Point> control_points{Message::Point(0.0, 0.0), Message::Point(kWidth - 1.0, kHeight - 1.0)};
    RegionState line_state(file_id, CARTA::RegionType::LINE, control_points, 0.0);
    if (!handler.SetRegion(region_id, line_state, frame->CoordinateSystem())) {
        return {};
    }
    CARTA::PvRequest request;
    request.set_file_id(file_id);
    request.set_region_id(region_id);
    request.set_width(3);
    GeneratedImage pv_image;
    handler.CalculatePvImage(request, frame, [](float) {}, response, pv_image);
    if (!pv_image.image) {
        return {};
    }
    const auto pixels = pv_image.image->get();
    return {pixels.begin(), pixels.end()};
}

// A loader that declines the boxes is answered one box at a time, and the image is the same.
TEST_F(ZarrImageTest, APvImageIsTheSameWhenTheLoaderDeclinesItsBoxes) {
    CARTA::PvResponse walked_response;
    CARTA::PvResponse looped_response;
    const auto walked = PvImagePixels(ScriptedZarrLoader::Script::walk, walked_response);
    const auto looped = PvImagePixels(ScriptedZarrLoader::Script::decline, looped_response);
    ASSERT_TRUE(walked_response.success()) << walked_response.message();
    ASSERT_TRUE(looped_response.success()) << looped_response.message();
    ASSERT_FALSE(walked.empty());
    ASSERT_EQ(walked.size(), looped.size());
    for (std::size_t i = 0; i < walked.size(); ++i) {
        if (std::isnan(looped[i])) {
            EXPECT_TRUE(std::isnan(walked[i])) << "pixel " << i;
        } else {
            EXPECT_FLOAT_EQ(walked[i], looped[i]) << "pixel " << i;
        }
    }
}

// A walk that fails once it has begun leaves no image, rather than one made again box by box.
TEST_F(ZarrImageTest, APvImageWhoseWalkFailsPartwayIsNotRetried) {
    CARTA::PvResponse response;
    EXPECT_TRUE(PvImagePixels(ScriptedZarrLoader::Script::fail_after_first_plane, response).empty());
    EXPECT_FALSE(response.success());
    EXPECT_FALSE(response.cancel());
}

// A rectangle's spectral profile is read by the loader, as every closed region's is. RegionHandler
// hands the loader a region only when it has a raster mask, and a rectangle was once thought to
// arrive as an LCBox without one and so to fall through to casacore's plane by plane statistics;
// it arrives as an LCPolygon with its raster, rotated or not.
TEST_F(ZarrImageTest, ARectanglesSpectralProfileIsReadByTheLoader) {
    for (const float rotation : {0.0f, 30.0f}) {
        auto loader = FileLoader::GetLoader(kZarrFixture.string());
        loader->OpenFile("");
        std::shared_ptr<Frame> frame(new Frame(0, loader, ""));
        ASSERT_TRUE(frame->IsValid());

        PeekableRegionHandler handler;
        const int file_id = 0;
        int region_id = -1;
        std::vector<CARTA::Point> control_points{Message::Point(1.5, 2.0), Message::Point(3.0, 4.0)};
        RegionState rectangle(file_id, CARTA::RegionType::RECTANGLE, control_points, rotation);
        ASSERT_TRUE(handler.SetRegion(region_id, rectangle, frame->CoordinateSystem()));

        CARTA::SetSpectralRequirements_SpectralConfig config;
        config.set_coordinate("z");
        config.add_stats_types(CARTA::StatsType::Mean);
        ASSERT_TRUE(handler.SetSpectralRequirements(region_id, file_id, frame, {config}));
        CARTA::SpectralProfileData profile;
        ASSERT_TRUE(handler.FillSpectralProfileData([&](CARTA::SpectralProfileData data) { profile = data; }, region_id, file_id, false));

        EXPECT_EQ(handler._region_profiles.Size(), 1u) << "rotation " << rotation << ": the profile should have been the loader's reading";
        EXPECT_FLOAT_EQ(profile.progress(), 1.0f);
        ASSERT_EQ(profile.profiles_size(), 1);
    }
}

// A region profile is resumed between steps and kept once made, until RegionHandler, which is what
// learns that a region is gone, lets it go.
TEST_F(ZarrImageTest, RemovingARegionThroughTheHandlerLetsItsProfileGo) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    loader->OpenFile("");
    std::shared_ptr<Frame> frame(new Frame(0, loader, ""));
    ASSERT_TRUE(frame->IsValid());

    PeekableRegionHandler handler;
    const int file_id = 0;
    handler._frames[file_id] = frame;

    std::vector<CARTA::Point> control_points{Message::Point(2.0, 2.0), Message::Point(2.0, 2.0)};
    RegionState rectangle(file_id, CARTA::RegionType::RECTANGLE, control_points, 0.0);
    int region_id = -1;
    ASSERT_TRUE(handler.SetRegion(region_id, rectangle, frame->CoordinateSystem()));

    casacore::Array<casacore::Bool> mask_2d(casacore::IPosition(2, kWidth, kHeight), true);
    casacore::ArrayLattice<casacore::Bool> mask(mask_2d);
    ProfilesMap profile;
    ASSERT_TRUE(WholeProfile(handler._region_profiles, *loader, region_id, 0, mask, casacore::IPosition(2, 0, 0), profile));
    ASSERT_EQ(handler._region_profiles.Size(), 1u);

    handler.RemoveRegion(region_id);
    EXPECT_EQ(handler._region_profiles.Size(), 0u) << "removing a region should let its profile go";
}

// An edit that leaves the region's bounding box and channel range alone still makes a different
// mask of them -- rotating a rectangle, dragging one vertex of a polygon inward -- and a profile is
// kept against that box and range. Unless the edit lets it go, a finished profile is handed back as it
// stood and a half-finished one resumed against the runs of the mask before the edit.
TEST_F(ZarrImageTest, EditingARegionThroughTheHandlerLetsItsProfileGo) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    loader->OpenFile("");
    std::shared_ptr<Frame> frame(new Frame(0, loader, ""));
    ASSERT_TRUE(frame->IsValid());

    PeekableRegionHandler handler;
    const int file_id = 0;
    handler._frames[file_id] = frame;

    std::vector<CARTA::Point> control_points{Message::Point(2.0, 2.0), Message::Point(4.0, 4.0)};
    RegionState rectangle(file_id, CARTA::RegionType::RECTANGLE, control_points, 0.0);
    int region_id = -1;
    ASSERT_TRUE(handler.SetRegion(region_id, rectangle, frame->CoordinateSystem()));

    // Two masks over the same box and the same channels: every pixel, then one column of them.
    casacore::Array<casacore::Bool> whole_2d(casacore::IPosition(2, kWidth, kHeight), true);
    casacore::Array<casacore::Bool> column_2d(casacore::IPosition(2, kWidth, kHeight), false);
    for (int y = 0; y < kHeight; ++y) {
        column_2d(casacore::IPosition(2, 0, y)) = true;
    }
    casacore::ArrayLattice<casacore::Bool> whole(whole_2d);
    casacore::ArrayLattice<casacore::Bool> column(column_2d);
    const casacore::IPosition origin(2, 0, 0);
    auto& profiles = handler._region_profiles;

    ProfilesMap before;
    ASSERT_TRUE(WholeProfile(profiles, *loader, region_id, 0, whole, origin, before));
    ASSERT_EQ(profiles.Size(), 1u);
    ASSERT_EQ(before[CARTA::StatsType::NumPixels].size(), static_cast<std::size_t>(kDepth));
    ASSERT_EQ(before[CARTA::StatsType::NumPixels][0], static_cast<double>(CountUnflagged(whole_2d)));

    // The edit keeps the bounding box the loader compares against, which is the whole point: a
    // rotation is what a user does to a region without moving or resizing it.
    RegionState rotated(file_id, CARTA::RegionType::RECTANGLE, control_points, 45.0);
    ASSERT_TRUE(handler.SetRegion(region_id, rotated, frame->CoordinateSystem()));
    EXPECT_EQ(profiles.Size(), 0u) << "editing a region should let its profile go";

    ProfilesMap after;
    ASSERT_TRUE(WholeProfile(profiles, *loader, region_id, 0, column, origin, after));
    ASSERT_EQ(after[CARTA::StatsType::NumPixels].size(), static_cast<std::size_t>(kDepth));
    EXPECT_EQ(after[CARTA::StatsType::NumPixels][0], static_cast<double>(CountUnflagged(column_2d)))
        << "the profile after the edit should count the pixels of the mask it was given";
    EXPECT_NE(after[CARTA::StatsType::Sum][0], before[CARTA::StatsType::Sum][0])
        << "a changed mask should not be answered with the sum of the one before it";
}

// The right chunk of the fixture is missing at z = 1, stokes = 2, so every one of its pixels is
// absent and the mean the PV generator would publish for that box is not a number.
TEST_F(ZarrImageTest, MultiRegionSpectralDataReportsAnEmptyChannel) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");

    const std::vector<RegionMaskSpec> regions{{2, 0, 2, kHeight, nullptr}};
    std::vector<double> counts(kDepth, -1.0);
    ASSERT_EQ(loader->RegionWalk()->RegionSpectra(regions, AxisRange(0, kDepth - 1), 2,
                  [&](const RegionSpectralBlock& block) {
                      for (std::size_t c = 0; c < block.channel_count; ++c) {
                          counts.at(block.first_channel + c) = block.NumPixels(0)[c];
                      }
                      return true;
                  }),
        BatchOutcome::finished);
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
    EXPECT_EQ(loader->RegionWalk()->RegionSpectra(regions, AxisRange(0, kDepth - 1), 0,
                  [&](const RegionSpectralBlock&) {
                      ++blocks;
                      return false;
                  }),
        BatchOutcome::cancelled);
    EXPECT_EQ(blocks, 1);
}

// A walk the library refuses is a failure, not a stop and not a refusal of the loader's own: the
// loader passed the box on, and it is the library that finds it runs off the image. Nothing is
// handed to the sink, because nothing was read.
TEST_F(ZarrImageTest, MultiRegionSpectralDataTheLibraryRefusesHasFailed) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");

    const std::vector<RegionMaskSpec> regions{{kWidth - 1, 0, 2, kHeight, nullptr}};
    int blocks = 0;
    EXPECT_EQ(loader->RegionWalk()->RegionSpectra(regions, AxisRange(0, kDepth - 1), 0,
                  [&](const RegionSpectralBlock&) {
                      ++blocks;
                      return true;
                  }),
        BatchOutcome::failed);
    EXPECT_EQ(blocks, 0);
}

// The batched path and the per-box path have to be the same answer, not just each plausible on its
// own. Both frames below read the same Zarr file: the first through ZarrLoader, which reduces every
// box at once, and the second through a loader holding the same CartaZarrImage, which has no
// batched path and so walks the boxes one at a time.
// A region's spectral profile now comes from the loader rather than from casacore iterating the
// image. The two have to be the same numbers: this builds one masked region, asks the loader for
// its profile, and asks casacore for the same region's statistics one channel at a time.
// A cursor profile now reports itself as it fills, so that a long one draws progressively and a
// cursor move can stop it. What the callback promises is that the leading fraction of the buffer is
// already final -- this fixture is one piece wide, so that promise, not the split, is what is
// pinned here.
TEST_F(ZarrImageTest, CursorSpectralDataReportsItsProgress) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");

    const int x = 3, y = 4, stokes = 1;
    std::mutex image_mutex;
    std::vector<float> profile;
    std::vector<float> reported;
    const auto read = loader->GetCursorSpectralData(profile, stokes, x, 1, y, 1, image_mutex, {},
        [&](float progress) {
            EXPECT_GT(progress, 0.0f);
            EXPECT_LE(progress, 1.0f);
            EXPECT_TRUE(reported.empty() || progress > reported.back()) << "progress should only grow";
            reported.push_back(progress);
            const auto finished = static_cast<std::size_t>(progress * profile.size() + 0.5f);
            for (std::size_t z = 0; z < finished; ++z) {
                const bool flagged = !ExpectedFlag(x, y) || InMissingChunk(x, z, stokes);
                if (flagged) {
                    EXPECT_TRUE(std::isnan(profile[z])) << "a reported channel should already be final at z=" << z;
                } else {
                    EXPECT_FLOAT_EQ(profile[z], ExpectedValue(x, y, z, stokes)) << "at z=" << z;
                }
            }
            return true;
        });
    ASSERT_EQ(read, BatchOutcome::finished);
    ASSERT_EQ(profile.size(), static_cast<std::size_t>(kDepth));
    ASSERT_FALSE(reported.empty());
    EXPECT_FLOAT_EQ(reported.back(), 1.0f) << "the last report should cover the whole profile";
}

// The same callback is how the read is stopped, because the caller checking between pieces is the
// only place that knows the cursor has moved.
TEST_F(ZarrImageTest, CursorSpectralDataStopsWhenTheCallbackDoes) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");

    std::mutex image_mutex;
    std::vector<float> profile;
    int calls = 0;
    const auto read = loader->GetCursorSpectralData(profile, 0, 1, 1, 1, 1, image_mutex, {},
        [&](float) {
            ++calls;
            return false;
        });
    EXPECT_EQ(read, BatchOutcome::cancelled);
    EXPECT_EQ(calls, 1);
}

// A read the caller cancelled is the caller's own decision, not a failure of the image. It used to
// be thrown like one, so every cursor move that stopped a profile part-way logged a warning. Read
// now hands back what the library said, and the library says which of the two it was.
TEST_F(ZarrImageTest, ACancelledReadSaysItWasCancelled) {
    CartaZarrImage image(kZarrFixture.string());
    carta::zarr::ReadOptions options;
    options.control.cancellation_requested = [] { return true; };
    casacore::Array<float> buffer;
    const casacore::Slicer section(casacore::IPosition(4, 0, 0, 0, 0), casacore::IPosition(4, 1, 1, kDepth, 1));

    const auto read = image.Read(buffer, section, options);
    ASSERT_FALSE(read) << "a cancelled read should say it did not finish";
    EXPECT_EQ(read.error().code, carta::zarr::ErrorCode::cancelled);
}

// A moment reads through a copy of the image whose reads keep what they decode in a cache of the
// moment's own. That copy and every copy made of it share the cache; the image it was copied from, and
// copies made before, read through the session's. Letting go of what holds the cache frees it, and the
// pixels are the same whichever cache answers.
TEST_F(ZarrImageTest, ACopyReadsThroughACacheOfItsOwnWhileOneIsHeld) {
    CartaZarrImage session(kZarrFixture.string());
    CartaZarrImage own(session);
    const CartaZarrImage made_before(own);
    const auto hold = own.OwnCache();
    std::unique_ptr<casacore::ImageInterface<float>> clone(own.cloneII());
    const auto* made_after = dynamic_cast<const CartaZarrImage*>(clone.get());
    ASSERT_NE(made_after, nullptr);
    EXPECT_FALSE(own.OwnCacheBytes()) << "nothing is held until the walk holds it";

    const std::size_t bytes = std::size_t(1) << 20;
    const casacore::Slicer section(casacore::IPosition(4, 0, 0, 0, 0), casacore::IPosition(4, kWidth, kHeight, kDepth, 1));
    {
        const auto held = hold(bytes);
        ASSERT_TRUE(held);
        EXPECT_EQ(own.OwnCacheBytes(), bytes);
        EXPECT_EQ(made_after->OwnCacheBytes(), bytes);
        EXPECT_FALSE(session.OwnCacheBytes());
        EXPECT_FALSE(made_before.OwnCacheBytes());

        const casacore::Array<float> through_own = own.getSlice(section);
        const casacore::Array<float> through_session = session.getSlice(section);
        ASSERT_EQ(through_own.shape(), through_session.shape());
        auto a = through_own.begin();
        for (auto b = through_session.begin(); b != through_session.end(); ++a, ++b) {
            EXPECT_TRUE((std::isnan(*a) && std::isnan(*b)) || *a == *b);
        }
    }
    EXPECT_FALSE(own.OwnCacheBytes()) << "the cache outlived what held it";
    EXPECT_FALSE(made_after->OwnCacheBytes());
}

// Whether the loader can read plane (z, stokes), however it says it cannot.
bool PlaneReads(FileLoader& loader, int z, int stokes, casacore::Array<float>& data) {
    const casacore::Slicer slicer(casacore::IPosition(4, 0, 0, z, stokes), casacore::IPosition(4, kWidth, kHeight, 1, 1));
    data.resize(slicer.length());
    try {
        return loader.GetSlice(data, StokesSlicer(StokesSource(), slicer));
    } catch (const std::exception&) {
        return false;
    }
}

// The fixture's chunks are one channel and one Stokes deep and half a row of the plane wide, and it has
// a flag, so a run is the plane, decoded at four bytes a pixel and one more for the flag.
TEST_F(ZarrImageTest, ARunIsThePlaneRoundedOutToChunksTimesTheirDepth) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    auto* read_ahead = loader->ReadAhead();
    ASSERT_NE(read_ahead, nullptr);
    EXPECT_EQ(read_ahead->RunOf(1, 2), (PlaneRun{1, 2}));
    EXPECT_EQ(read_ahead->RunBytes(), kWidth * kHeight * (sizeof(float) + 1));
}

// A copy of the fixture whose SKY metadata is rewritten by `edit`, and without the nodes named in
// `dropped`. Only the metadata: what a run costs is answered before any pixel is read.
std::filesystem::path EditedFixture(const std::string& name, const std::function<std::string(const std::string&)>& edit,
    const std::vector<std::string>& dropped = {}) {
    const auto copy = TestRoot() / "data" / "generated" / name;
    std::filesystem::remove_all(copy);
    std::filesystem::copy(kZarrFixture, copy, std::filesystem::copy_options::recursive);
    for (const auto& node : dropped) {
        std::filesystem::remove_all(copy / node);
    }
    const auto metadata = copy / "SKY" / "zarr.json";
    std::stringstream text;
    text << std::ifstream(metadata).rdbuf();
    std::ofstream(metadata, std::ios::trunc) << edit(text.str());
    return copy;
}

// The cache holds a chunk as it is stored, not as the float it is read out as, so a float64 image's
// run is eight bytes a pixel and its flag's one more. Counted at four, two runs looked as if they fit
// a cache they did not, and reading ahead evicted the chunks the animation was playing.
TEST_F(ZarrImageTest, ARunIsCountedAtTheWidthTheChunksAreStoredIn) {
    const auto copy = EditedFixture("float64_run.zarr", [](const std::string& text) {
        return std::regex_replace(text, std::regex(R"("data_type"\s*:\s*"float32")"), R"("data_type": "float64")");
    });
    auto loader = FileLoader::GetLoader(copy.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    auto* read_ahead = loader->ReadAhead();
    ASSERT_NE(read_ahead, nullptr);
    EXPECT_EQ(read_ahead->RunBytes(), kWidth * kHeight * (sizeof(double) + 1));
}

// And a run counts a flag only when the image has one. CartaZarrImage reports a mask whatever the
// store holds -- casacore's statistics need it to -- so that is not the question to ask.
TEST_F(ZarrImageTest, ARunCountsAFlagOnlyWhenTheImageHasOne) {
    const auto copy = EditedFixture(
        "unflagged_run.zarr",
        [](const std::string& text) { return std::regex_replace(text, std::regex(R"("flag"\s*:\s*"FLAG"\s*,)"), ""); },
        {"FLAG"});
    auto loader = FileLoader::GetLoader(copy.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    auto* read_ahead = loader->ReadAhead();
    ASSERT_NE(read_ahead, nullptr);
    EXPECT_EQ(read_ahead->RunBytes(), kWidth * kHeight * sizeof(float));
}

// What a prefetch decoded, the plane's read finds without going to storage: after it the chunks are
// emptied on disk, and the plane still reads as it did, flags and all, while the next plane, which was
// not prefetched, no longer reads at all.
TEST_F(ZarrImageTest, APrefetchedPlaneIsReadWithoutGoingToStorage) {
    const auto copy = TestRoot() / "data" / "generated" / "prefetched.zarr";
    std::filesystem::remove_all(copy);
    std::filesystem::copy(kZarrFixture, copy, std::filesystem::copy_options::recursive);

    casacore::Array<float> before;
    {
        auto reference = FileLoader::GetLoader(copy.string());
        reference->OpenFile("");
        ASSERT_TRUE(PlaneReads(*reference, 0, 1, before));
    }

    auto loader = FileLoader::GetLoader(copy.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    auto* image = dynamic_cast<CartaZarrImage*>(loader->GetImage().get());
    ASSERT_NE(image, nullptr);
    // A cache of the image's own, so that the test does not size the process's.
    const std::uint64_t bytes = std::uint64_t(64) << 20;
    const auto held = image->OwnCache()(bytes);
    ASSERT_TRUE(held);
    auto* read_ahead = loader->ReadAhead();
    ASSERT_NE(read_ahead, nullptr);
    EXPECT_EQ(read_ahead->CacheBytes(), bytes);
    ASSERT_TRUE(read_ahead->Prefetch(0, 1, [] { return false; }));

    for (const char* array : {"SKY", "FLAG"}) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(copy / array / "c")) {
            if (entry.is_regular_file()) {
                std::filesystem::resize_file(entry.path(), 0);
            }
        }
    }

    casacore::Array<float> after;
    ASSERT_TRUE(PlaneReads(*loader, 0, 1, after)) << "a prefetched plane went to storage for its chunks";
    ASSERT_EQ(after.shape(), before.shape());
    auto a = after.begin();
    for (auto b = before.begin(); b != before.end(); ++a, ++b) {
        EXPECT_TRUE((std::isnan(*a) && std::isnan(*b)) || *a == *b);
    }
    casacore::Array<float> next;
    EXPECT_FALSE(PlaneReads(*loader, 1, 1, next)) << "a plane not prefetched still read from emptied chunks, so this shows nothing";
    EXPECT_FALSE(read_ahead->Prefetch(0, kStokes, [] { return false; })) << "a Stokes the image does not have was prefetched";
    std::filesystem::remove_all(copy);
}

// A whole-cube histogram counts in 64 bits and the backend holds a bin as an int. A 2.9e11-pixel
// cube needs under 1% of its pixels in one bin to pass INT_MAX, and a narrowing cast wrapped that to
// a negative count. No fixture is that large, so the conversion is checked on its own.
TEST(ZarrBinCounts, ACountPastIntMaxIsHeldThereRatherThanWrapped) {
    constexpr auto kIntMax = std::numeric_limits<int>::max();
    const std::vector<std::uint64_t> counts{5, static_cast<std::uint64_t>(kIntMax), std::uint64_t{1} << 31,
        std::uint64_t{1} << 40};
    std::vector<int> bins;
    EXPECT_TRUE(AssignBinCounts(counts.data(), counts.size(), bins)) << "two of these do not fit in an int";
    EXPECT_EQ(bins, (std::vector<int>{5, kIntMax, kIntMax, kIntMax}));

    const std::vector<std::uint64_t> small{0, 1, 2};
    EXPECT_FALSE(AssignBinCounts(small.data(), small.size(), bins)) << "nothing here needed holding";
    EXPECT_EQ(bins, (std::vector<int>{0, 1, 2}));
}

// A region covering a large image takes tens of seconds to read one chunk layer, and the caller's
// checks between calls all come too late. The loader reports through the callback while the call is
// still working, carrying partial sums that converge; the finished profile must be unaffected by
// having been looked at on the way.
// Every plane's statistics in one pass agree with reading each plane and reducing it.
TEST_F(ZarrImageTest, CubeBasicStatsAgreeWithThePerPlaneLoop) {
    ExpectPlaneStatsAgreeWithThePerPlaneLoop(kZarrFixture);
}

// The same for planes at the edges of what can be derived: one valid pixel, whose sigma a plane
// reports as NaN where a profile would say zero, two that are equal, and a few.
TEST_F(ZarrImageTest, PlaneStatisticsOfALonePixelAgreeWithThePerPlaneLoop) {
    WrittenZarrFixture fixture("lone_pixel_planes.zarr");
    fixture.Put(0, 1, 0, 0, 0.5f);
    fixture.Put(0, 1, 1, 0, 1.0f);
    fixture.Put(0, 2, 1, 0, 2.0f);
    fixture.Put(1, 4, 1, 0, 4.0f);
    fixture.Put(3, 1, 1, 0, -3.0f);
    fixture.Put(3, 2, 0, 1, -0.25f);
    fixture.Put(0, 2, 1, 1, 6.0f);
    fixture.Put(0, 1, 0, 2, 1.5f);
    fixture.Put(0, 2, 0, 2, 1.5f);
    fixture.Put(3, 2, 1, 2, 3.0f);
    ExpectPlaneStatsAgreeWithThePerPlaneLoop(fixture.Write());
}

// A copy of the fixture with planes that have no valid pixel: one beside a plane that has some, and a
// whole stokes of them.
std::filesystem::path FixtureWithEmptyPlanes() {
    WrittenZarrFixture fixture("empty_planes.zarr");
    fixture.Put(0, 1, 1, 0, 1.0f);
    fixture.Put(0, 2, 1, 0, -2.0f);
    fixture.Put(0, 1, 0, 2, 0.5f);
    fixture.Put(3, 2, 0, 2, 4.0f);
    fixture.Put(0, 2, 1, 2, 3.0f);
    return fixture.Write();
}

// And for planes with no valid pixel, whose mean, sigma and RMS are undefined whichever route found it.
TEST_F(ZarrImageTest, PlaneStatisticsOfAnEmptyPlaneAgreeWithThePerPlaneLoop) {
    ExpectPlaneStatsAgreeWithThePerPlaneLoop(FixtureWithEmptyPlanes());
}

// A cube with no valid pixel has nothing derived either, and says so the same way whether its
// statistics came from the loader's walk, from the planes one at a time, or from one pass.
TEST_F(ZarrImageTest, ACubeWithNoValidPixelHasTheSameStatisticsWhicheverRouteMadeIt) {
    const auto path = FixtureWithEmptyPlanes();
    constexpr int kEmptyStokes = 1;
    CubeHistogramMethod one_pass;
    one_pass.one_pass = true;

    const std::vector<std::pair<std::string, std::pair<ScriptedZarrLoader::Script, CubeHistogramMethod>>> routes = {
        {"walk", {ScriptedZarrLoader::Script::walk, CubeHistogramMethod()}},
        {"plane by plane", {ScriptedZarrLoader::Script::no_walks, CubeHistogramMethod()}},
        {"one pass", {ScriptedZarrLoader::Script::walk, one_pass}}};
    for (const auto& [name, route] : routes) {
        auto frame = ScriptedFrame(route.first, path);
        ASSERT_TRUE(frame->IsValid()) << name;
        BasicStats<float> stats;
        Histogram histogram;
        ASSERT_EQ(frame->CalculateCubeHistogram(kEmptyStokes, CubeConfig(9), route.second, {}, {}, stats, histogram),
            CubeHistogramOutcome::finished)
            << name;
        EXPECT_EQ(stats.num_pixels, 0u) << name;
        EXPECT_TRUE(std::isnan(stats.mean)) << name << ": mean is " << stats.mean;
        EXPECT_TRUE(std::isnan(stats.stdDev)) << name << ": sigma is " << stats.stdDev;
        EXPECT_TRUE(std::isnan(stats.rms)) << name << ": rms is " << stats.rms;
    }
}

// A cube histogram is the same whichever route made it: the loader's two walks, or the planes read
// one at a time when the loader declines. Counts are integers and the range is the data's own
// extremes, so the bins have to agree exactly; so does what is cached, since a later request is
// answered from the cache without asking which route filled it.
TEST_F(ZarrImageTest, ACubeHistogramIsTheSameWhicheverRouteMadeIt) {
    const auto config = CubeConfig(9);
    for (int stokes = 0; stokes < kStokes; ++stokes) {
        auto walked_frame = ScriptedFrame(ScriptedZarrLoader::Script::walk);
        auto looped_frame = ScriptedFrame(ScriptedZarrLoader::Script::no_walks);
        ASSERT_TRUE(walked_frame->IsValid());
        ASSERT_TRUE(looped_frame->IsValid());

        // What a caller is told on the way is the calculator's to get right, and is tested there. That
        // Frame gave it the depth of this cube is not: the last plane of the statistics is the last
        // report before the halfway mark, and a fraction of the whole bar only if the depth was right.
        std::vector<float> walked_reports, looped_reports;
        const auto collect = [](std::vector<float>& into) {
            return [&into](const CubeHistogramProgress& update) {
                into.push_back(update.progress);
                return true;
            };
        };

        BasicStats<float> walked_stats, looped_stats;
        Histogram walked, looped;
        ASSERT_EQ(walked_frame->CalculateCubeHistogram(stokes, config, CubeHistogramMethod(), {}, collect(walked_reports), walked_stats, walked),
            CubeHistogramOutcome::finished);
        ASSERT_EQ(looped_frame->CalculateCubeHistogram(stokes, config, CubeHistogramMethod(), {}, collect(looped_reports), looped_stats, looped),
            CubeHistogramOutcome::finished)
            << "a loader with no walk is answered plane by plane, not refused";
        for (const auto* reports : {&walked_reports, &looped_reports}) {
            ASSERT_EQ(reports->size(), static_cast<std::size_t>((kDepth * 2) + 1)) << "a report per plane of each half, and one between";
            EXPECT_FLOAT_EQ((*reports)[kDepth - 1], static_cast<float>(kDepth - 1) / static_cast<float>(kDepth * 2));
            EXPECT_FLOAT_EQ((*reports)[kDepth], 0.5f);
        }

        EXPECT_EQ(walked_stats.num_pixels, looped_stats.num_pixels) << "stokes " << stokes;
        EXPECT_FLOAT_EQ(walked_stats.min_val, looped_stats.min_val) << "stokes " << stokes;
        EXPECT_FLOAT_EQ(walked_stats.max_val, looped_stats.max_val) << "stokes " << stokes;
        EXPECT_NEAR(walked_stats.sum, looped_stats.sum, 1e-6 * std::abs(looped_stats.sum)) << "stokes " << stokes;
        EXPECT_EQ(walked.GetHistogramBins(), looped.GetHistogramBins()) << "stokes " << stokes;

        for (const auto& frame : {walked_frame, looped_frame}) {
            BasicStats<float> cached;
            ASSERT_TRUE(frame->GetBasicStats(ALL_Z, stokes, cached)) << "the cube statistics are cached once final";
            EXPECT_EQ(cached.num_pixels, looped_stats.num_pixels);
        }
    }
}

// A stop leaves nothing cached on either route, whether it arrives through the predicate asked
// between reads or through the progress callback.
TEST_F(ZarrImageTest, ACancelledCubeHistogramCachesNothing) {
    const int stokes = 1;
    for (auto script : {ScriptedZarrLoader::Script::walk, ScriptedZarrLoader::Script::no_walks}) {
        auto by_predicate = ScriptedFrame(script);
        bool stop = false;
        BasicStats<float> stats;
        Histogram histogram;
        EXPECT_EQ(by_predicate->CalculateCubeHistogram(
                      stokes, CubeConfig(9), CubeHistogramMethod(), [&]() { return stop; },
                      [&](const CubeHistogramProgress&) {
                          stop = true;
                          return true;
                      },
                      stats, histogram),
            CubeHistogramOutcome::cancelled);

        auto by_callback = ScriptedFrame(script);
        EXPECT_EQ(
            by_callback->CalculateCubeHistogram(
                stokes, CubeConfig(9), CubeHistogramMethod(), {}, [](const CubeHistogramProgress&) { return false; }, stats, histogram),
            CubeHistogramOutcome::cancelled);

        for (const auto& frame : {by_predicate, by_callback}) {
            BasicStats<float> cached;
            EXPECT_FALSE(frame->GetBasicStats(ALL_Z, stokes, cached));
        }
    }
}

// A stop during the bins leaves the statistics found in the first half no more cached than a stop
// during the first half does: a cube histogram is made whole or not at all.
TEST_F(ZarrImageTest, ACubeHistogramCancelledDuringItsBinsCachesNothing) {
    const int stokes = 1;
    for (auto script : {ScriptedZarrLoader::Script::walk, ScriptedZarrLoader::Script::no_walks}) {
        auto frame = ScriptedFrame(script);
        bool halfway = false;
        BasicStats<float> stats;
        Histogram histogram;
        EXPECT_EQ(frame->CalculateCubeHistogram(
                      stokes, CubeConfig(9), CubeHistogramMethod(), {},
                      [&](const CubeHistogramProgress& update) {
                          if (halfway) {
                              return false;
                          }
                          halfway = update.milestone;
                          return true;
                      },
                      stats, histogram),
            CubeHistogramOutcome::cancelled);
        EXPECT_TRUE(halfway) << "the stop came after the statistics were found";

        BasicStats<float> cached;
        EXPECT_FALSE(frame->GetBasicStats(ALL_Z, stokes, cached));
    }
}

// Each plane's statistics are kept on the way past, whichever route found them: the plane by plane
// route keeps them, and the walk that stands in for it has to, since a request for one plane looks
// there before it reads the plane.
TEST_F(ZarrImageTest, ACubeHistogramLeavesEachPlanesStatisticsWhicheverRouteMadeIt) {
    const int stokes = 1;
    std::map<ScriptedZarrLoader::Script, std::shared_ptr<PeekableFrame>> frames;
    for (auto script : {ScriptedZarrLoader::Script::walk, ScriptedZarrLoader::Script::no_walks}) {
        auto loader = std::make_shared<ScriptedZarrLoader>(kZarrFixture.string(), script);
        loader->OpenFile("");
        auto frame = std::make_shared<PeekableFrame>(0, loader, "");
        ASSERT_TRUE(frame->IsValid());

        BasicStats<float> stats;
        Histogram histogram;
        ASSERT_EQ(frame->CalculateCubeHistogram(stokes, CubeConfig(9), CubeHistogramMethod(), {}, {}, stats, histogram),
            CubeHistogramOutcome::finished);
        EXPECT_EQ(frame->_image_basic_stats.size(), static_cast<std::size_t>(kDepth));
        for (int z = 0; z < kDepth; ++z) {
            EXPECT_EQ(frame->_image_basic_stats.count(frame->CacheKey(z, stokes)), 1u) << "plane " << z;
        }
        frames[script] = frame;
    }

    for (int z = 0; z < kDepth; ++z) {
        const auto key = frames[ScriptedZarrLoader::Script::walk]->CacheKey(z, stokes);
        const auto& walked = frames[ScriptedZarrLoader::Script::walk]->_image_basic_stats.at(key);
        const auto& looped = frames[ScriptedZarrLoader::Script::no_walks]->_image_basic_stats.at(key);
        EXPECT_EQ(walked.num_pixels, looped.num_pixels) << "plane " << z;
        EXPECT_FLOAT_EQ(walked.min_val, looped.min_val) << "plane " << z;
        EXPECT_FLOAT_EQ(walked.max_val, looped.max_val) << "plane " << z;
        EXPECT_NEAR(walked.sum, looped.sum, 1e-6 * (1.0 + std::abs(looped.sum))) << "plane " << z;
    }
}

// What Frame hands the calculator is the method and the bounds it was given: one pass is asked of the
// loader only when the method asks for it and the request leaves the bounds free. That it is the
// calculator's rule is tested there; that Frame does not lose the request on the way is tested here.
TEST_F(ZarrImageTest, AFrameHandsTheCalculatorTheMethodAndTheBoundsItWasGiven) {
    const int stokes = 1;
    CubeHistogramMethod one_pass;
    one_pass.one_pass = true;
    auto fixed = CubeConfig(9);
    fixed.fixed_bounds = true;
    fixed.bounds = HistogramBounds(0.0, 4000.0);
    BasicStats<float> stats;
    Histogram histogram;

    auto loader = std::make_shared<OnePassCountingLoader>(kZarrFixture.string());
    loader->OpenFile("");
    Frame frame(0, loader, "");
    ASSERT_TRUE(frame.IsValid());
    ASSERT_EQ(
        frame.CalculateCubeHistogram(stokes, CubeConfig(9), CubeHistogramMethod(), {}, {}, stats, histogram), CubeHistogramOutcome::finished);
    EXPECT_EQ(loader->one_pass_calls, 0) << "exact is two passes";
    ASSERT_EQ(frame.CalculateCubeHistogram(stokes, fixed, one_pass, {}, {}, stats, histogram), CubeHistogramOutcome::finished);
    EXPECT_EQ(loader->one_pass_calls, 0) << "fixed bounds are two passes";
    ASSERT_EQ(frame.CalculateCubeHistogram(stokes, CubeConfig(9), one_pass, {}, {}, stats, histogram), CubeHistogramOutcome::finished);
    EXPECT_EQ(loader->one_pass_calls, 1);
}

// Every plane's bin counts in one pass, against the per-plane path that reads the plane and bins
// it. Counts are integers, so unlike the statistics these have to agree exactly.
TEST_F(ZarrImageTest, CubeHistogramAgreesWithThePerPlaneLoop) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    std::shared_ptr<Frame> frame(new Frame(0, loader, ""));
    ASSERT_TRUE(frame->IsValid());

    const int num_bins = 9;
    const HistogramBounds bounds(0.0, 4000.0);
    for (int stokes = 0; stokes < kStokes; ++stokes) {
        std::map<int, std::vector<int>> from_loader;
        ASSERT_EQ(loader->CubeWalk()->PlaneHistograms(stokes, num_bins, bounds, {},
                      [&](int z, const Histogram& histogram) {
                          EXPECT_EQ(from_loader.count(z), 0u) << "plane " << z << " was reported twice";
                          EXPECT_EQ(histogram.GetNbins(), static_cast<std::size_t>(num_bins));
                          EXPECT_EQ(histogram.GetBounds(), bounds) << "laid over the bounds it was asked to bin over";
                          from_loader[z] = histogram.GetHistogramBins();
                          return true;
                      }),
            BatchOutcome::finished)
            << "the Zarr loader should bin the cube itself";
        ASSERT_EQ(from_loader.size(), static_cast<std::size_t>(kDepth));

        for (int z = 0; z < kDepth; ++z) {
            Histogram reference;
            ASSERT_TRUE(frame->CalculateHistogram(CUBE_REGION_ID, z, stokes, num_bins, bounds, reference));
            const auto& expected = reference.GetHistogramBins();
            ASSERT_EQ(from_loader[z].size(), expected.size());
            for (std::size_t bin = 0; bin < expected.size(); ++bin) {
                EXPECT_EQ(from_loader[z][bin], expected[bin])
                    << " bin=" << bin << " z=" << z << " stokes=" << stokes;
            }
        }
    }
}

// One pass over the cube, against the two the caller would otherwise run. Whether to make it is the
// session's decision, so here it is simply asked for.
TEST_F(ZarrImageTest, CubeHistogramOnePassAgreesWithTwo) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    std::shared_ptr<Frame> frame(new Frame(0, loader, ""));
    ASSERT_TRUE(frame->IsValid());

    const int num_bins = 7;
    const int stokes = 1;
    BasicStats<float> stats;
    std::vector<int> bins;
    EXPECT_EQ(loader->CubeWalk()->OnePassCubeHistogram(stokes, num_bins, 0, stats, bins, {}), BatchOutcome::declined)
        << "a stride of zero reads no pixel, and it no longer means 'whatever the settings say'";

    ASSERT_EQ(loader->CubeWalk()->OnePassCubeHistogram(stokes, num_bins, 1, stats, bins, {}), BatchOutcome::finished);
    ASSERT_EQ(bins.size(), static_cast<std::size_t>(num_bins));

    // The two-pass answer over the range the one pass found.
    BasicStats<float> two_pass_stats;
    for (int z = 0; z < kDepth; ++z) {
        BasicStats<float> plane;
        ASSERT_TRUE(frame->GetBasicStats(z, stokes, plane));
        two_pass_stats.join(plane);
    }
    EXPECT_EQ(stats.num_pixels, two_pass_stats.num_pixels);
    EXPECT_FLOAT_EQ(stats.min_val, two_pass_stats.min_val) << "the extremes are exact in either method";
    EXPECT_FLOAT_EQ(stats.max_val, two_pass_stats.max_val);
    EXPECT_NEAR(stats.sum, two_pass_stats.sum, 1e-9 * (1.0 + std::abs(two_pass_stats.sum)));

    const HistogramBounds bounds(two_pass_stats.min_val, two_pass_stats.max_val);
    Histogram two_pass;
    for (int z = 0; z < kDepth; ++z) {
        Histogram plane;
        ASSERT_TRUE(frame->CalculateHistogram(CUBE_REGION_ID, z, stokes, num_bins, bounds, plane));
        if (z == 0) {
            two_pass = plane;
        } else {
            two_pass.Add(plane);
        }
    }

    // Bin edges are where the two are allowed to differ, so what is required is that no pixel was
    // lost and that the shape is the same.
    std::int64_t one_pass_total = 0;
    std::int64_t two_pass_total = 0;
    for (std::size_t bin = 0; bin < bins.size(); ++bin) {
        one_pass_total += bins[bin];
        two_pass_total += two_pass.GetHistogramBins()[bin];
    }
    EXPECT_EQ(one_pass_total, static_cast<std::int64_t>(stats.num_pixels)) << "every pixel should be in a bin";
    EXPECT_EQ(one_pass_total, two_pass_total);
    for (std::size_t bin = 0; bin < bins.size(); ++bin) {
        EXPECT_EQ(bins[bin], two_pass.GetHistogramBins()[bin]) << " bin=" << bin;
    }
}

// One pass reports a histogram of what it has read, not only a fraction, so the frontend can redraw
// as it fills in -- which is what the two-pass path does from its halfway mark on.
//
// The budget is what makes this testable at all: the walk reports between reads, and the fixture is
// small enough to be read in one go unless it is told otherwise.
TEST_F(ZarrImageTest, CubeHistogramOnePassReportsAsItGoes) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    auto* zarr_loader = dynamic_cast<ZarrLoader*>(loader.get());
    ASSERT_NE(zarr_loader, nullptr);
    zarr_loader->SetReadBudgetBytes(1);

    const int num_bins = 7;
    const int stokes = 1;
    BasicStats<float> stats;
    std::vector<int> bins;

    int updates = 0;
    double last_progress = -1.0;
    std::size_t last_pixels = 0;
    ASSERT_EQ(loader->CubeWalk()->OnePassCubeHistogram(stokes, num_bins, 1, stats, bins,
                  [&](const CubeHistogramUpdate& update) {
                      ++updates;
                      EXPECT_GT(update.progress, last_progress) << "progress should not go backwards";
                      last_progress = update.progress;

                      BasicStats<float> partial_stats;
                      std::vector<int> partial_bins;
                      update.snapshot(partial_stats, partial_bins);
                      EXPECT_EQ(partial_bins.size(), static_cast<std::size_t>(num_bins));
                      std::int64_t total = 0;
                      for (const auto count : partial_bins) {
                          total += count;
                      }
                      EXPECT_EQ(total, static_cast<std::int64_t>(partial_stats.num_pixels))
                          << "a partial histogram should hold every pixel it has counted";
                      EXPECT_GE(partial_stats.num_pixels, last_pixels) << "a partial cannot un-read a pixel";
                      last_pixels = partial_stats.num_pixels;
                      if (partial_stats.num_pixels > 0) {
                          EXPECT_LE(partial_stats.min_val, partial_stats.max_val);
                      }
                      return true;
                  }),
        BatchOutcome::finished);

    EXPECT_GT(updates, 0) << "a one-byte budget should have taken several reads and reported on each";
    EXPECT_GE(stats.num_pixels, last_pixels) << "the answer should hold at least what the last partial did";

    // And saying stop ends the walk, which the loader reports as having no answer.
    BasicStats<float> ignored_stats;
    std::vector<int> ignored_bins;
    EXPECT_EQ(loader->CubeWalk()->OnePassCubeHistogram(
                  stokes, num_bins, 1, ignored_stats, ignored_bins, [](const CubeHistogramUpdate&) { return false; }),
        BatchOutcome::cancelled);
}

// A range the walk cannot express is declined rather than answered differently: the caller's
// degenerate case is a single bin over [0, 0], which is not a histogram this produces.
TEST_F(ZarrImageTest, CubeHistogramDeclinesAnEmptyRange) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    int calls = 0;
    auto count = [&](int, const Histogram&) {
        ++calls;
        return true;
    };
    EXPECT_EQ(loader->CubeWalk()->PlaneHistograms(0, 8, HistogramBounds(5.0, 5.0), {}, count), BatchOutcome::declined);
    EXPECT_EQ(loader->CubeWalk()->PlaneHistograms(0, 0, HistogramBounds(0.0, 10.0), {}, count), BatchOutcome::declined);
    EXPECT_EQ(calls, 0) << "a declined request should not have reported a plane";
}

// A callback that says stop ends the walk rather than being asked for the next plane.
TEST_F(ZarrImageTest, CubeBasicStatsStopWhenTheCallbackDoes) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    int calls = 0;
    EXPECT_EQ(loader->CubeWalk()->PlaneStats(0, {},
                  [&](int, const BasicStats<float>&) {
                      ++calls;
                      return false;
                  }),
        BatchOutcome::cancelled);
    EXPECT_EQ(calls, 1) << "a callback that says stop should not be asked again";
}

// A stop asked for while a plane is still being read ends the walk there, rather than when the plane
// is finished. On a large image one plane is every read of its chunk layer, seconds of them, and the
// callback that used to be the only place a stop was noticed is reached only after all of them.
//
// A one-byte budget makes every chunk its own read, so the first plane cannot be finished by the
// first; the cancellation says yes from its second question on, which is after that first read.
TEST_F(ZarrImageTest, CubeBasicStatsStopBetweenReadsWhenCancelled) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    auto* zarr_loader = dynamic_cast<ZarrLoader*>(loader.get());
    ASSERT_NE(zarr_loader, nullptr);
    zarr_loader->SetReadBudgetBytes(1);

    int asked = 0;
    int planes = 0;
    EXPECT_EQ(loader->CubeWalk()->PlaneStats(
                  0, [&]() { return ++asked > 1; },
                  [&](int, const BasicStats<float>&) {
                      ++planes;
                      return true;
                  }),
        BatchOutcome::cancelled);
    EXPECT_GT(asked, 1) << "the walk should have asked again after its first read";
    EXPECT_EQ(planes, 0) << "the stop should have ended the walk before any plane was finished";
}

// The same for bin counts, which walk the planes the same way and were stopped the same late way.
TEST_F(ZarrImageTest, CubeHistogramStopsBetweenReadsWhenCancelled) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    auto* zarr_loader = dynamic_cast<ZarrLoader*>(loader.get());
    ASSERT_NE(zarr_loader, nullptr);
    zarr_loader->SetReadBudgetBytes(1);

    int asked = 0;
    int planes = 0;
    EXPECT_EQ(loader->CubeWalk()->PlaneHistograms(
                  0, 9, HistogramBounds(0.0, 4000.0), [&]() { return ++asked > 1; },
                  [&](int, const Histogram&) {
                      ++planes;
                      return true;
                  }),
        BatchOutcome::cancelled);
    EXPECT_GT(asked, 1) << "the walk should have asked again after its first read";
    EXPECT_EQ(planes, 0) << "the stop should have ended the walk before any plane was finished";
}

TEST_F(ZarrImageTest, RegionSpectralDataReportsWhileItIsStillWorking) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    auto* zarr_loader = dynamic_cast<ZarrLoader*>(loader.get());
    ASSERT_NE(zarr_loader, nullptr);
    // One byte, so every chunk is its own read and the reduction cannot finish in one.
    zarr_loader->SetReadBudgetBytes(1);

    const int x0 = 1, y0 = 1, region_width = 3, region_height = 4;
    casacore::Array<casacore::Bool> mask_2d(casacore::IPosition(2, region_width, region_height), true);
    casacore::ArrayLattice<casacore::Bool> mask_lattice(mask_2d);
    const casacore::IPosition origin(2, x0, y0);
    std::mutex image_mutex;

    std::vector<float> reported;
    ProfilesMap from_loader;
    RegionProfiles profiles;
    const RegionProfileReport report = [&](float partial_progress, const std::function<ProfilesMap()>& partial) {
        EXPECT_EQ(partial().count(CARTA::StatsType::Sum), 1u) << "a partial profile should carry the stats";
        reported.push_back(partial_progress);
        return true;
    };
    ASSERT_TRUE(WholeProfile(profiles, *loader, 0, 0, mask_lattice, origin, from_loader, report)) << "the loader profile never completed";
    ASSERT_FALSE(reported.empty()) << "a one-byte budget should have made the reduction report on the way";
    for (const float value : reported) {
        EXPECT_GE(value, 0.0f);
        EXPECT_LT(value, 1.0f) << "a report from inside the call is never the finished profile";
    }

    // The same profile without ever looking at it.
    auto reference_loader = FileLoader::GetLoader(kZarrFixture.string());
    reference_loader->OpenFile("");
    RegionProfiles reference_profiles;
    ProfilesMap reference;
    ASSERT_TRUE(WholeProfile(reference_profiles, *reference_loader, 0, 0, mask_lattice, origin, reference));
    for (const auto& [stat, values] : reference) {
        ASSERT_EQ(from_loader[stat].size(), values.size()) << "stat=" << static_cast<int>(stat);
        for (std::size_t z = 0; z < values.size(); ++z) {
            const std::string where = " stat=" + std::to_string(static_cast<int>(stat)) + " z=" + std::to_string(z);
            if (std::isnan(values[z])) {
                EXPECT_TRUE(std::isnan(from_loader[stat][z])) << where;
            } else {
                EXPECT_NEAR(from_loader[stat][z], values[z], 1e-9 * (1.0 + std::abs(values[z]))) << where;
            }
        }
    }
}

// A step pauses at the end of a run once its time is up, and the next goes on from there: a profile
// made in the shortest steps there are is the profile made in one.
TEST_F(ZarrImageTest, AProfileMadeInTheShortestStepsIsTheSameProfile) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    auto* zarr_loader = dynamic_cast<ZarrLoader*>(loader.get());
    ASSERT_NE(zarr_loader, nullptr);
    // One byte, so that every channel is a run of its own and there is somewhere to pause.
    zarr_loader->SetReadBudgetBytes(1);

    casacore::Array<casacore::Bool> mask_2d(casacore::IPosition(2, 3, 4), true);
    casacore::ArrayLattice<casacore::Bool> mask(mask_2d);
    const casacore::IPosition origin(2, 1, 1);

    RegionProfiles profiles;
    ProfilesMap stepped;
    float progress = 0.0f;
    int steps = 0;
    while (progress < 1.0f) {
        ASSERT_EQ(ProfileStep(profiles, *loader, 0, 2, mask, origin, stepped, progress, {}, std::chrono::milliseconds(0)),
            BatchOutcome::finished);
        ASSERT_LT(++steps, 100) << "the profile never completed";
    }
    EXPECT_GT(steps, 1) << "a step with no time to spare should have paused before the last channel";

    RegionProfiles whole_profiles;
    ProfilesMap whole;
    ASSERT_TRUE(WholeProfile(whole_profiles, *loader, 0, 2, mask, origin, whole));
    for (const auto& [stat, values] : whole) {
        for (std::size_t z = 0; z < values.size(); ++z) {
            if (std::isnan(values[z])) {
                EXPECT_TRUE(std::isnan(stepped.at(stat)[z])) << "stat=" << static_cast<int>(stat) << " z=" << z;
            } else {
                EXPECT_EQ(stepped.at(stat)[z], values[z]) << "stat=" << static_cast<int>(stat) << " z=" << z;
            }
        }
    }
}

// A callback that says stop ends the call rather than being asked again.
TEST_F(ZarrImageTest, RegionSpectralDataStopsWhenTheCallbackDoes) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    auto* zarr_loader = dynamic_cast<ZarrLoader*>(loader.get());
    ASSERT_NE(zarr_loader, nullptr);
    zarr_loader->SetReadBudgetBytes(1);

    const int region_width = 3, region_height = 4;
    casacore::Array<casacore::Bool> mask_2d(casacore::IPosition(2, region_width, region_height), true);
    casacore::ArrayLattice<casacore::Bool> mask_lattice(mask_2d);
    std::mutex image_mutex;

    ProfilesMap from_loader;
    float progress = 0.0;
    int calls = 0;
    RegionProfiles profiles;
    EXPECT_EQ(ProfileStep(profiles, *loader, 0, 0, mask_lattice, casacore::IPosition(2, 1, 1), from_loader, progress,
                  [&](float, const std::function<ProfilesMap()>&) {
                      ++calls;
                      return false;
                  }),
        BatchOutcome::cancelled);
    EXPECT_EQ(calls, 1) << "a callback that says stop should not be asked again";
}

// A step takes up to TARGET_PARTIAL_REGION_TIME, and the loader used to hold one lock over every
// region's state for all of it. Removing or editing any other region then waited
// for that walk, and so did every other region's profile. Released from another thread because on
// this one the old lock was not merely slow but a self-deadlock.
TEST_F(ZarrImageTest, ReleasingARegionDoesNotWaitForAnotherRegionsWalk) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    auto* zarr_loader = dynamic_cast<ZarrLoader*>(loader.get());
    ASSERT_NE(zarr_loader, nullptr);
    // One byte, so the walk reports from inside itself and there is a moment to release during.
    zarr_loader->SetReadBudgetBytes(1);

    casacore::Array<casacore::Bool> mask_2d(casacore::IPosition(2, kWidth, kHeight), true);
    casacore::ArrayLattice<casacore::Bool> mask_lattice(mask_2d);
    std::mutex image_mutex;

    // Kept out here because a future from std::async waits for its task when destroyed: dropped
    // inside the callback, a release that is stuck behind the walk would stall the walk itself.
    std::vector<std::future<void>> releases;
    bool released_in_time = true;
    RegionProfiles profiles;
    ProfilesMap from_loader;
    ASSERT_TRUE(WholeProfile(profiles, *loader, 0, 0, mask_lattice, casacore::IPosition(2, 0, 0), from_loader,
        [&](float, const std::function<ProfilesMap()>&) {
            releases.push_back(std::async(std::launch::async, [&] { profiles.Release(1); }));
            if (releases.back().wait_for(std::chrono::seconds(2)) != std::future_status::ready) {
                released_in_time = false;
            }
            return true;
        }))
        << "the loader profile never completed";
    ASSERT_FALSE(releases.empty()) << "a one-byte budget should have made the walk report on the way";
    EXPECT_TRUE(released_in_time) << "releasing region 1 should not wait for region 0's walk";
}

TEST_F(ZarrImageTest, RegionSpectralDataAgreesWithCasacore) {
    auto loader = FileLoader::GetLoader(kZarrFixture.string());
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    auto image = loader->GetImage();
    ASSERT_NE(image, nullptr);

    // A region straddling the chunk boundary at x = 2, with a mask that is neither empty nor full.
    const int x0 = 1, y0 = 1, region_width = 3, region_height = 4;
    casacore::Array<casacore::Bool> mask(casacore::IPosition(4, region_width, region_height, 1, 1));
    for (int y = 0; y < region_height; ++y) {
        for (int x = 0; x < region_width; ++x) {
            mask(casacore::IPosition(4, x, y, 0, 0)) = ((x + (2 * y)) % 3) != 0;
        }
    }
    casacore::Array<casacore::Bool> mask_2d(casacore::IPosition(2, region_width, region_height));
    for (int y = 0; y < region_height; ++y) {
        for (int x = 0; x < region_width; ++x) {
            mask_2d(casacore::IPosition(2, x, y)) = mask(casacore::IPosition(4, x, y, 0, 0));
        }
    }
    casacore::ArrayLattice<casacore::Bool> mask_lattice(mask_2d);

    const std::vector<CARTA::StatsType> compared{CARTA::StatsType::NumPixels, CARTA::StatsType::Sum,
        CARTA::StatsType::Mean, CARTA::StatsType::RMS, CARTA::StatsType::Sigma, CARTA::StatsType::Min,
        CARTA::StatsType::Max};

    for (int stokes = 0; stokes < kStokes; ++stokes) {
        ASSERT_NE(loader->ProfileReader(), nullptr) << "the Zarr loader should read region profiles itself";
        RegionProfiles profiles;
        ProfilesMap from_loader;
        ASSERT_TRUE(WholeProfile(profiles, *loader, stokes, stokes, mask_lattice, casacore::IPosition(2, x0, y0), from_loader))
            << "the loader profile failed at stokes=" << stokes;

        for (int z = 0; z < kDepth; ++z) {
            casacore::LCBox box(casacore::IPosition(4, x0, y0, z, stokes),
                casacore::IPosition(4, x0 + region_width - 1, y0 + region_height - 1, z, stokes), image->shape());
            casacore::SubImage<float> sub_image(*image, casacore::LCPixelSet(mask, box), false);

            std::map<CARTA::StatsType, std::vector<double>> from_casacore;
            ASSERT_TRUE(CalcStatsValues(from_casacore, compared, sub_image, false));

            for (const auto stat : compared) {
                const double loader_value = from_loader[stat][z];
                const double casacore_value = from_casacore[stat][0];
                const std::string where = " stat=" + std::to_string(static_cast<int>(stat)) +
                                          " z=" + std::to_string(z) + " stokes=" + std::to_string(stokes);
                if (std::isnan(casacore_value)) {
                    EXPECT_TRUE(std::isnan(loader_value)) << where;
                } else {
                    EXPECT_NEAR(loader_value, casacore_value, 1e-9 * (1.0 + std::abs(casacore_value))) << where;
                }
            }
        }
    }
}

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
// A beam table with one row per plane is how XRADIO stores beams whether or not the planes differ,
// so the loader has to decide which it is. The decision is not cosmetic: hasMultipleBeams() is what
// makes a consumer reconcile the planes, and ImageMoments reconciles by convolving the whole cube
// to a common beam before computing anything, materialising a full-size copy of the input.
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
    EXPECT_EQ(info.getBeamSet().nelements(), 6u);  // three channels by two polarizations
}

namespace {

std::vector<std::string> ListedImages(const std::filesystem::path& store) {
    FileInfoCache::Instance().Clear();
    CARTA::FileInfo file_info;
    FileInfoLoader(store.string(), CARTA::FileType::ZARR).FillFileInfo(file_info);
    return {file_info.hdu_list().begin(), file_info.hdu_list().end()};
}

}  // namespace

// The file list is what a user chooses from, so every image on it has to open. The library's
// openable flag promises that much of the library; CARTA refuses more than the library does, and
// the list used to ask only the library.
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

// Two times is a valid XRADIO dataset and the library opens every image in it. CARTA displays one
// time, so it offers none of them -- and the file is still a Zarr dataset, with a reason to give
// when someone opens it anyway.
TEST_F(ZarrImageTest, ADatasetWithTwoTimesListsNoImage) {
    const std::filesystem::path fixture{ZARR_XRADIO_TIME_AXIS_FIXTURE};
    if (!std::filesystem::exists(fixture)) {
        GTEST_SKIP() << "xradio time-axis fixture not found at " << fixture;
    }
    EXPECT_TRUE(ListedImages(fixture).empty()) << "an image CARTA cannot open should not be offered";
    try {
        CartaZarrImage image(fixture.string());
        ADD_FAILURE() << "an image with two times should not open";
    } catch (const casacore::AipsError& error) {
        EXPECT_NE(error.getMesg().find("singleton time axis"), std::string::npos) << error.getMesg();
    }
    FileInfoCache::Instance().Clear();
}

// ---------------------------------------------------------------------------
// Measurement, not a test. Skipped unless CARTA_PLANE_STORE names a store.
//
//   CARTA_PLANE_STORE=<path>      the image to step through
//   CARTA_PLANE_CHANNELS=<n>      how many channels to visit (default 12)
//   CARTA_PLANE_CACHE_MB=<n>      decoded-chunk cache, as the server sizes it
//
// This is the interactive path: every channel change refills the whole image
// cache at full resolution. Reported per channel, because a store chunked more
// than one channel deep should make the first of each group expensive and the
// rest nearly free -- and whether it actually does is the question.
// ---------------------------------------------------------------------------
TEST_F(ZarrImageTest, MeasurePlaneReads) {
    const char* store_env = std::getenv("CARTA_PLANE_STORE");
    if (store_env == nullptr || *store_env == '\0') {
        GTEST_SKIP() << "set CARTA_PLANE_STORE to measure";
    }
    const std::string store(store_env);

    const char* cache_env = std::getenv("CARTA_PLANE_CACHE_MB");
    const int cache_mb = cache_env ? std::stoi(cache_env) : ZARR_CACHE_POOL_MB;
    const char* io_env = std::getenv("CARTA_PLANE_IO_THREADS");
    const int io_threads = io_env ? std::stoi(io_env) : ZARR_FILE_IO_CONCURRENCY;
    carta::ConfigureZarrContext(io_threads, ZARR_DATA_COPY_CONCURRENCY, cache_mb, omp_get_num_procs());

    auto loader = FileLoader::GetLoader(store);
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    std::shared_ptr<Frame> frame(new Frame(0, loader, ""));
    ASSERT_TRUE(frame->IsValid());

    const char* count_env = std::getenv("CARTA_PLANE_CHANNELS");
    const int channels = count_env ? std::stoi(count_env) : 12;
    const double pixels = static_cast<double>(frame->Width()) * static_cast<double>(frame->Height());

    std::printf("\nPLANE store=%s %zux%zu cache_MiB=%d io_threads=%d\n", store.c_str(), frame->Width(),
        frame->Height(), cache_mb, io_threads);
    // From one, not zero: a Frame opens at z 0 with its cache already filled, and
    // SetImageChannels declines a channel that is not a change.
    const char* first_env = std::getenv("CARTA_PLANE_FIRST");
    const int first = first_env ? std::stoi(first_env) : 1;
    for (int z = first; z < first + channels; ++z) {
        const auto before = ReadIo();
        const auto t0 = std::chrono::steady_clock::now();
        std::string message;
        ASSERT_TRUE(frame->SetImageChannels(z, 0, message)) << message;
        const auto t1 = std::chrono::steady_clock::now();
        const auto after = ReadIo();

        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        const double mib = 1024.0 * 1024.0;
        std::printf("PLANE z=%-4d %8.1f ms %8.1f MPix/s  read %7.1f MiB  disk %7.1f MiB  reads %6lld\n", z,
            ms, pixels / (ms * 1000.0), static_cast<double>(after.rchar - before.rchar) / mib,
            static_cast<double>(after.read_bytes - before.read_bytes) / mib, after.syscr - before.syscr);
        std::fflush(stdout);
    }
}

// ---------------------------------------------------------------------------
// Measurement, not a test. Skipped unless CARTA_SHAPE_STORE names a store.
//
// One read of a whole plane against one read per chunk row. Both decode exactly
// the same chunks -- a band is a whole chunk tall, so no chunk is touched twice --
// so the only difference is where the decoded pixels land.
//
// The destination is x-fastest, and these stores are y-fastest, so filling a plane
// is a transposing scatter. Done in one call it scatters across the whole plane;
// done a band at a time it scatters inside a band, which is contiguous in the
// destination and two orders of magnitude smaller.
//
// The decoded-chunk cache is off, so every repeat does the same work and the page
// cache is the only thing kept warm. Variants alternate and the median is reported,
// because this project has twice drawn the opposite conclusion from too few reps.
// ---------------------------------------------------------------------------
TEST_F(ZarrImageTest, MeasurePlaneReadShapes) {
    const char* store_env = std::getenv("CARTA_SHAPE_STORE");
    if (store_env == nullptr || *store_env == '\0') {
        GTEST_SKIP() << "set CARTA_SHAPE_STORE to measure";
    }
    carta::ConfigureZarrContext(ZARR_FILE_IO_CONCURRENCY, ZARR_DATA_COPY_CONCURRENCY, 0, omp_get_num_procs());

    auto loader = FileLoader::GetLoader(store_env);
    ASSERT_NE(loader, nullptr);
    loader->OpenFile("");
    auto image = std::dynamic_pointer_cast<CartaZarrImage>(loader->GetImage());
    ASSERT_NE(image, nullptr);

    const auto shape = image->shape();
    const auto width = static_cast<int>(shape(0));
    const auto height = static_cast<int>(shape(1));
    const auto band = static_cast<int>(image->niceCursorShape(image->advisedMaxPixels())(1));
    ASSERT_GT(band, 0);
    std::vector<float> plane(static_cast<std::size_t>(width) * height);

    const char* mult_env = std::getenv("CARTA_SHAPE_BAND_CHUNKS");
    const int band_chunks = mult_env ? std::max(1, std::stoi(mult_env)) : 1;
    const int band_rows = band * band_chunks;
    // Every XRADIO store varies y fastest while the destination varies x fastest, so filling a
    // plane transposes -- which is the whole reason a destination-shaped read might have helped.

    const int reps = 5;
    std::vector<double> whole;
    std::vector<double> bands;
    for (int rep = 0; rep < reps; ++rep) {
        for (int variant = 0; variant < 2; ++variant) {
            const auto t0 = std::chrono::steady_clock::now();
            if (variant == 0) {
                casacore::Slicer section(casacore::IPosition(4, 0, 0, 0, 0), casacore::IPosition(4, width, height, 1, 1));
                casacore::Array<float> view(section.length(), plane.data(), casacore::StorageInitPolicy::SHARE);
                ASSERT_TRUE(image->Read(view, section));
            } else {
                for (int y = 0; y < height; y += band_rows) {
                    const int rows = std::min(band_rows, height - y);
                    casacore::Slicer section(
                        casacore::IPosition(4, 0, y, 0, 0), casacore::IPosition(4, width, rows, 1, 1));
                    // A run of rows is contiguous in an x-fastest destination, so this shares the
                    // image buffer rather than copying out of a temporary.
                    casacore::Array<float> view(section.length(),
                        plane.data() + (static_cast<std::size_t>(y) * width), casacore::StorageInitPolicy::SHARE);
                    ASSERT_TRUE(image->Read(view, section));
                }
            }
            const auto t1 = std::chrono::steady_clock::now();
            (variant == 0 ? whole : bands).push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        }
    }

    auto median = [](std::vector<double> v) {
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };
    const double pixels = static_cast<double>(width) * height;
    std::printf("SHAPE store=%s %dx%d band=%d x%d reps=%d\n", store_env, width, height, band, band_chunks, reps);
    std::printf("SHAPE whole-plane  median %7.1f ms  %8.1f MPix/s\n", median(whole), pixels / (median(whole) * 1000.0));
    std::printf("SHAPE chunk-bands  median %7.1f ms  %8.1f MPix/s  (%d reads)\n", median(bands),
        pixels / (median(bands) * 1000.0), (height + band_rows - 1) / band_rows);
    std::fflush(stdout);
}
