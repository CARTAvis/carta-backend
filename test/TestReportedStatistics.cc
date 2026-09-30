/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// The statistics CARTA reports, and that they are made of the totals the same call reports beside them.
//
// Each loader that derives them says so in its own code, and this is what they have to keep saying
// however that code is arranged: the mean, RMS, sigma and extrema of a plane, a cube or a region's
// channel are functions of its count, sum, sum of squares, smallest and largest, and of nothing else.
// So the reported statistics are compared bit for bit against a derivation written out here from the
// totals that were reported with them, and the totals against the pixels, which the loader does not
// choose.

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <casacore/lattices/Lattices/ArrayLattice.h>
#include <gtest/gtest.h>

#include "ImageData/FileLoader.h"
#include "src/Frame/Frame.h"

#include "CommonTestUtilities.h"

using namespace carta;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// What a pixel-by-pixel count of a region's channel comes to.
struct CountedTotals {
    double num_pixels = 0.0;
    double sum = 0.0;
    double sum_sq = 0.0;
    double min = std::numeric_limits<double>::max();
    double max = std::numeric_limits<double>::lowest();
};

struct Reported {
    double mean;
    double rms;
    double sigma;
    double extrema;
};

// The derivation, as the loaders each wrote it. `lone_pixel_sigma` is what to say for one valid pixel;
// a profile says zero and a plane or a cube says NaN.
Reported ReportedFrom(const CountedTotals& t, double lone_pixel_sigma) {
    if (t.num_pixels == 0.0) {
        return {kNaN, kNaN, kNaN, kNaN};
    }
    Reported reported;
    reported.mean = t.sum / t.num_pixels;
    reported.rms = std::sqrt(t.sum_sq / t.num_pixels);
    reported.sigma =
        t.num_pixels > 1.0 ? std::sqrt((t.sum_sq - (t.sum * t.sum / t.num_pixels)) / (t.num_pixels - 1.0)) : lone_pixel_sigma;
    reported.extrema = std::abs(t.min) > std::abs(t.max) ? t.min : t.max;
    return reported;
}

// The same for a set of pixels, in the simplest way there is.
CountedTotals CountPixels(const std::vector<float>& pixels) {
    CountedTotals counted;
    for (const float pixel : pixels) {
        const double v = pixel;
        if (std::isfinite(v)) {
            counted.num_pixels += 1.0;
            counted.sum += v;
            counted.sum_sq += v * v;
            counted.min = std::min(counted.min, v);
            counted.max = std::max(counted.max, v);
        }
    }
    return counted;
}

// One channel of a region of a 3D image, from the copy of the file that a spectral profile is read from:
// x, then y, then the channel varying fastest. The loader chooses how to add these up, and does not
// choose what they are.
CountedTotals CountRegionChannel(const fs::path& path, int x_min, int y_min, int width, int height, int channel) {
    H5::H5File file(path.string(), H5F_ACC_RDONLY);
    auto dataset = file.openDataSet("/0/PermutedData/ZYX");
    const hsize_t start[3] = {static_cast<hsize_t>(x_min), static_cast<hsize_t>(y_min), static_cast<hsize_t>(channel)};
    const hsize_t count[3] = {static_cast<hsize_t>(width), static_cast<hsize_t>(height), 1};
    auto file_space = dataset.getSpace();
    file_space.selectHyperslab(H5S_SELECT_SET, count, start);

    hsize_t size = static_cast<hsize_t>(width) * height;
    std::vector<float> pixels(size);
    H5::DataSpace memory_space(1, &size);
    dataset.read(pixels.data(), H5::PredType::NATIVE_FLOAT, memory_space, file_space);
    return CountPixels(pixels);
}

// A whole plane of a 3D image, from the file's own copy of it.
CountedTotals CountPlane(const fs::path& path, int width, int height, int channel) {
    Hdf5DataReader reader(path.string());
    return CountPixels(reader.ReadRegion({0, 0, static_cast<hsize_t>(channel)},
        {static_cast<hsize_t>(width), static_cast<hsize_t>(height), static_cast<hsize_t>(channel + 1)}));
}

// EXPECT_EQ that a NaN equals a NaN, since a statistic that is undefined is reported as one.
void ExpectSame(double actual, double expected, const std::string& what) {
    if (std::isnan(expected)) {
        EXPECT_TRUE(std::isnan(actual)) << what << " should be undefined but is " << actual;
    } else {
        EXPECT_EQ(actual, expected) << what;
    }
}

void ExpectNear(double actual, double expected, double relative, const std::string& what) {
    if (std::isnan(expected)) {
        EXPECT_TRUE(std::isnan(actual)) << what << " should be undefined but is " << actual;
    } else {
        EXPECT_NEAR(actual, expected, relative * (1.0 + std::abs(expected))) << what;
    }
}

// An image opened as a session opens it: the frame is what tells the loader what shape it has, and
// what asks it for the statistics it keeps.
struct OpenedImage {
    std::shared_ptr<FileLoader> loader;
    std::unique_ptr<Frame> frame;

    explicit OpenedImage(const fs::path& path) : loader(FileLoader::GetLoader(path)) {
        if (loader) {
            frame = std::make_unique<Frame>(0, loader, "0");
        }
    }

    bool IsValid() const {
        return frame && frame->IsValid();
    }
};

// The statistics a spectral profile of a rectangular region reports, from a fresh loader.
bool ProfileOf(const fs::path& path, int depth, int x_min, int y_min, int width, int height,
    std::map<CARTA::StatsType, std::vector<double>>& profile) {
    OpenedImage image(path);
    if (!image.IsValid()) {
        return false;
    }

    casacore::Array<casacore::Bool> mask_2d(casacore::IPosition(2, width, height), true);
    casacore::ArrayLattice<casacore::Bool> mask(mask_2d);
    const casacore::IPosition origin(2, x_min, y_min);
    std::mutex image_mutex;

    // A call takes as many columns as it takes, and says how far it has got.
    constexpr int kMostCalls = 1000;
    float progress = 0.0;
    for (int calls = 0; progress < 1.0; ++calls) {
        if (calls > kMostCalls ||
            !image.loader->GetRegionSpectralData(1, AxisRange(0, depth - 1), 0, mask, origin, image_mutex, profile, progress)) {
            return false;
        }
    }
    return true;
}

// A profile against the pixels it was made from, and against the totals it reports beside its
// statistics. The first tells us the totals are the region's; the second is the derivation.
void ExpectProfileIsDerivedFromItsTotals(const fs::path& path, int depth, int x, int y, int width, int height) {
    std::map<CARTA::StatsType, std::vector<double>> profile;
    ASSERT_TRUE(ProfileOf(path, depth, x, y, width, height, profile));

    for (int z = 0; z < depth; ++z) {
        const std::string where = path.filename().string() + " z=" + std::to_string(z);
        const auto pixels = CountRegionChannel(path, x, y, width, height, z);
        const auto at = [&](CARTA::StatsType stat) { return profile.at(stat).at(z); };

        ASSERT_EQ(at(CARTA::StatsType::NumPixels), pixels.num_pixels) << where;

        CountedTotals reported;
        reported.num_pixels = at(CARTA::StatsType::NumPixels);
        if (reported.num_pixels == 0.0) {
            // Nothing is defined for a channel with no valid pixel, whatever it was going to be made of.
            for (const auto stat : {CARTA::StatsType::Sum, CARTA::StatsType::SumSq, CARTA::StatsType::Min, CARTA::StatsType::Max,
                     CARTA::StatsType::Mean, CARTA::StatsType::RMS, CARTA::StatsType::Sigma, CARTA::StatsType::Extrema}) {
                EXPECT_TRUE(std::isnan(at(stat))) << where << " stat=" << static_cast<int>(stat);
            }
            continue;
        }
        reported.sum = at(CARTA::StatsType::Sum);
        reported.sum_sq = at(CARTA::StatsType::SumSq);
        reported.min = at(CARTA::StatsType::Min);
        reported.max = at(CARTA::StatsType::Max);

        // The sums are accumulated in an order of the loader's choosing, so they are the pixels' up
        // to that; the extremes are not sums, and are the pixels' exactly.
        ExpectNear(reported.sum, pixels.sum, 1e-9, where + " sum");
        ExpectNear(reported.sum_sq, pixels.sum_sq, 1e-9, where + " sum_sq");
        EXPECT_EQ(reported.min, pixels.min) << where;
        EXPECT_EQ(reported.max, pixels.max) << where;

        // A profile says zero for one pixel.
        const auto expected = ReportedFrom(reported, 0.0);
        ExpectSame(at(CARTA::StatsType::Mean), expected.mean, where + " mean");
        ExpectSame(at(CARTA::StatsType::RMS), expected.rms, where + " rms");
        ExpectSame(at(CARTA::StatsType::Sigma), expected.sigma, where + " sigma");
        ExpectSame(at(CARTA::StatsType::Extrema), expected.extrema, where + " extrema");
    }
}

// What a loader that keeps statistics in the file reports for the planes and the cube.
struct StoredStats {
    OpenedImage image;

    explicit StoredStats(const fs::path& path) : image(path) {}

    bool IsValid() const {
        return image.IsValid();
    }

    // z below zero is the cube.
    FileInfo::ImageStats& Of(int z) const {
        return image.loader->GetImageStats(0, z);
    }

    std::map<CARTA::StatsType, double> At(int z) const {
        return Of(z).basic_stats;
    }
};

void ExpectStoredDerivedFromItsTotals(const std::map<CARTA::StatsType, double>& stats, const CountedTotals& pixels, const std::string& where) {
    CountedTotals reported;
    reported.num_pixels = stats.at(CARTA::StatsType::NumPixels);
    reported.sum = stats.at(CARTA::StatsType::Sum);
    reported.sum_sq = stats.at(CARTA::StatsType::SumSq);
    reported.min = stats.at(CARTA::StatsType::Min);
    reported.max = stats.at(CARTA::StatsType::Max);

    // What the file stored is the pixels', to the precision it stored it in.
    ASSERT_EQ(reported.num_pixels, pixels.num_pixels) << where;
    ExpectNear(reported.sum, pixels.sum, 1e-5, where + " sum");
    ExpectNear(reported.sum_sq, pixels.sum_sq, 1e-5, where + " sum_sq");
    EXPECT_EQ(reported.min, pixels.min) << where;
    EXPECT_EQ(reported.max, pixels.max) << where;

    // A plane or a cube says NaN for one pixel.
    const auto expected = ReportedFrom(reported, kNaN);
    ExpectSame(stats.at(CARTA::StatsType::Mean), expected.mean, where + " mean");
    ExpectSame(stats.at(CARTA::StatsType::RMS), expected.rms, where + " rms");
    ExpectSame(stats.at(CARTA::StatsType::Sigma), expected.sigma, where + " sigma");
    ExpectSame(stats.at(CARTA::StatsType::Extrema), expected.extrema, where + " extrema");
}

} // namespace

class ReportedStatisticsTest : public ::testing::Test {};

// The pixels a channel of a region has, whether or not there are any of them.

TEST_F(ReportedStatisticsTest, AProfileOfAWholePlaneIsDerivedFromItsTotals) {
    ExpectProfileIsDerivedFromItsTotals(Hdf5Images() / "10x10x10.hdf5", 10, 0, 0, 10, 10);
}

TEST_F(ReportedStatisticsTest, AProfileOverPixelsThatAreNotAllFiniteIsDerivedFromItsTotals) {
    // 90 valid pixels in the first channel and 72 in the second.
    ExpectProfileIsDerivedFromItsTotals(Hdf5Images() / "10x10x2_nans.hdf5", 2, 0, 0, 10, 10);
}

TEST_F(ReportedStatisticsTest, AProfileOfASubRegionIsDerivedFromItsTotals) {
    ExpectProfileIsDerivedFromItsTotals(Hdf5Images() / "10x10x10.hdf5", 10, 2, 3, 5, 4);
}

TEST_F(ReportedStatisticsTest, AProfileOfOnePixelHasSigmaZero) {
    const auto path = Hdf5Images() / "10x10x2_nans.hdf5";
    ExpectProfileIsDerivedFromItsTotals(path, 2, 1, 0, 1, 1);

    std::map<CARTA::StatsType, std::vector<double>> profile;
    ASSERT_TRUE(ProfileOf(path, 2, 1, 0, 1, 1, profile));
    for (int z = 0; z < 2; ++z) {
        EXPECT_EQ(profile.at(CARTA::StatsType::NumPixels).at(z), 1.0) << "z=" << z;
        EXPECT_EQ(profile.at(CARTA::StatsType::Sigma).at(z), 0.0) << "one valid pixel has no spread to report, and a profile says zero";
        EXPECT_EQ(profile.at(CARTA::StatsType::Mean).at(z), profile.at(CARTA::StatsType::Extrema).at(z)) << "z=" << z;
    }
}

TEST_F(ReportedStatisticsTest, AProfileChannelWithNoValidPixelIsUndefinedBesideOneThatHasSome) {
    // The fourth row of the first plane is all NaN; the same row of the second is not.
    const auto path = Hdf5Images() / "10x10x2_nans.hdf5";
    ExpectProfileIsDerivedFromItsTotals(path, 2, 0, 3, 10, 1);

    std::map<CARTA::StatsType, std::vector<double>> profile;
    ASSERT_TRUE(ProfileOf(path, 2, 0, 3, 10, 1, profile));
    EXPECT_EQ(profile.at(CARTA::StatsType::NumPixels).at(0), 0.0);
    EXPECT_GT(profile.at(CARTA::StatsType::NumPixels).at(1), 0.0);
}

// The extreme of larger magnitude wins, whichever end of the range it is at.

TEST_F(ReportedStatisticsTest, ExtremaIsTheExtremeOfLargerMagnitude) {
    // Four pixels along one row: the first plane's are -0.661528, 0.93505, 0.0490546 and 2.00239, so its
    // largest outweighs its smallest; the second's are -1.47389, 1.02885, NaN and -0.239937, so its
    // smallest outweighs its largest.
    std::map<CARTA::StatsType, std::vector<double>> profile;
    ASSERT_TRUE(ProfileOf(Hdf5Images() / "10x10x2_nans.hdf5", 2, 6, 7, 4, 1, profile));
    EXPECT_NEAR(profile.at(CARTA::StatsType::Extrema).at(0), 2.00239, 1e-5);
    EXPECT_NEAR(profile.at(CARTA::StatsType::Extrema).at(1), -1.47389, 1e-5);
    ExpectProfileIsDerivedFromItsTotals(Hdf5Images() / "10x10x2_nans.hdf5", 2, 6, 7, 4, 1);
}

// A file that keeps its statistics is trusted for its totals and derived from them.

TEST_F(ReportedStatisticsTest, StoredPlaneStatisticsAreDerivedFromTheirTotals) {
    const auto path = Hdf5Images() / "10x10x10.hdf5";
    StoredStats stored(path);
    ASSERT_TRUE(stored.IsValid());
    for (int z = 0; z < 10; ++z) {
        ASSERT_TRUE(stored.Of(z).valid) << "z=" << z;
        ASSERT_TRUE(stored.Of(z).full) << "z=" << z;
        ExpectStoredDerivedFromItsTotals(stored.At(z), CountPlane(path, 10, 10, z), "plane " + std::to_string(z));
    }
}

TEST_F(ReportedStatisticsTest, StoredPlaneStatisticsOfPlanesWithNonFinitePixelsAreDerivedFromTheirTotals) {
    const auto path = Hdf5Images() / "10x10x2_nans.hdf5";
    StoredStats stored(path);
    ASSERT_TRUE(stored.IsValid());
    for (int z = 0; z < 2; ++z) {
        ASSERT_TRUE(stored.Of(z).full) << "z=" << z;
        ExpectStoredDerivedFromItsTotals(stored.At(z), CountPlane(path, 10, 10, z), "plane " + std::to_string(z));
    }
}

TEST_F(ReportedStatisticsTest, StoredCubeStatisticsAreDerivedFromTheirTotals) {
    const std::vector<std::pair<std::string, int>> cubes = {{"10x10x10.hdf5", 10}, {"10x10x2_nans.hdf5", 2}};
    for (const auto& [name, depth] : cubes) {
        const auto path = Hdf5Images() / name;
        StoredStats stored(path);
        ASSERT_TRUE(stored.IsValid());
        ASSERT_TRUE(stored.Of(-1).full) << name;

        // The cube is every plane's pixels together.
        CountedTotals cube;
        for (int z = 0; z < depth; ++z) {
            const auto plane = CountPlane(path, 10, 10, z);
            cube.num_pixels += plane.num_pixels;
            cube.sum += plane.sum;
            cube.sum_sq += plane.sum_sq;
            cube.min = std::min(cube.min, plane.min);
            cube.max = std::max(cube.max, plane.max);
        }
        ExpectStoredDerivedFromItsTotals(stored.At(-1), cube, std::string("cube of ") + name);
    }
}
