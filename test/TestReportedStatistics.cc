/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// The statistics CARTA reports, and that they are made of the totals the same call reports beside them,
// and the module that makes them.
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
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <casacore/images/Images/SubImage.h>
#include <casacore/lattices/LRegions/LCBox.h>
#include <casacore/lattices/Lattices/ArrayLattice.h>
#include <gtest/gtest.h>

#include "ImageData/FileLoader.h"
#include "ImageStats/BasicStatsCalculator.h"
#include "ImageStats/DerivedStatistics.h"
#include "ImageStats/StatsCalculator.h"
#include "Region/RegionAnalysis/RegionProfiles.h"
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
    // From the mean, found first, when the totals say it: the Zarr loader's do, no other loader's.
    std::optional<double> sum_sq_dev;
};

struct Reported {
    double mean;
    double rms;
    double sigma;
    double extrema;
};

// The derivation, as the loaders each wrote it -- with sigma made from the spread instead where the
// totals have one. `lone_pixel_sigma` is what to say for one valid pixel; a profile says zero and a
// plane or a cube says NaN.
Reported ReportedFrom(const CountedTotals& t, double lone_pixel_sigma) {
    if (t.num_pixels == 0.0) {
        return {kNaN, kNaN, kNaN, kNaN};
    }
    Reported reported;
    reported.mean = t.sum / t.num_pixels;
    reported.rms = std::sqrt(t.sum_sq / t.num_pixels);
    if (t.num_pixels <= 1.0) {
        reported.sigma = lone_pixel_sigma;
    } else if (t.sum_sq_dev) {
        reported.sigma = std::sqrt(std::max(*t.sum_sq_dev, 0.0) / (t.num_pixels - 1.0));
    } else {
        reported.sigma = std::sqrt((t.sum_sq - (t.sum * t.sum / t.num_pixels)) / (t.num_pixels - 1.0));
    }
    reported.extrema = std::abs(t.min) > std::abs(t.max) ? t.min : t.max;
    return reported;
}

// The same totals with their spread, from the mean found first, as the Zarr loader's carry one.
CountedTotals WithSpread(CountedTotals counted, const std::vector<float>& pixels) {
    double sum_sq_dev = 0.0;
    if (counted.num_pixels > 0.0) {
        const double mean = counted.sum / counted.num_pixels;
        for (const float pixel : pixels) {
            if (std::isfinite(pixel)) {
                sum_sq_dev += (pixel - mean) * (pixel - mean);
            }
        }
    }
    counted.sum_sq_dev = sum_sq_dev;
    return counted;
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
    RegionProfileRequest request;
    request.origin = casacore::IPosition(2, x_min, y_min);
    request.mask = &mask;
    request.channels = AxisRange(0, depth - 1);
    auto* reader = image.loader->ProfileReader();
    if (!reader) {
        return false;
    }
    RegionProfiles profiles;
    std::mutex image_mutex;

    // A step takes as many columns as it takes, and says how far it has got.
    constexpr int kMostSteps = 1000;
    float progress = 0.0;
    for (int steps = 0; progress < 1.0; ++steps) {
        if (steps > kMostSteps || profiles.Continue({0, 1, 0}, request, *reader, image_mutex, std::chrono::milliseconds(1000), {}, profile,
                                      progress) != BatchOutcome::finished) {
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
        EXPECT_EQ(at(CARTA::StatsType::NanCount), (width * height) - pixels.num_pixels) << where;

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

void ExpectStoredDerivedFromItsTotals(
    const std::map<CARTA::StatsType, double>& stats, const CountedTotals& pixels, const std::string& where) {
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
    // The fourth row of the first plane is all NaN; the same row of the second is not. Two pixels of
    // it, since a region much wider than it is tall times deep is left to casacore.
    const auto path = Hdf5Images() / "10x10x2_nans.hdf5";
    ExpectProfileIsDerivedFromItsTotals(path, 2, 0, 3, 2, 1);

    std::map<CARTA::StatsType, std::vector<double>> profile;
    ASSERT_TRUE(ProfileOf(path, 2, 0, 3, 2, 1, profile));
    EXPECT_EQ(profile.at(CARTA::StatsType::NumPixels).at(0), 0.0);
    EXPECT_GT(profile.at(CARTA::StatsType::NumPixels).at(1), 0.0);
}

// The extreme of larger magnitude wins, whichever end of the range it is at.

TEST_F(ReportedStatisticsTest, ExtremaIsTheExtremeOfLargerMagnitude) {
    // Two pixels along one row: the first plane's are -0.661528 and 0.93505, so its largest outweighs
    // its smallest; the second's are -1.47389 and 1.02885, so its smallest outweighs its largest.
    std::map<CARTA::StatsType, std::vector<double>> profile;
    ASSERT_TRUE(ProfileOf(Hdf5Images() / "10x10x2_nans.hdf5", 2, 6, 7, 2, 1, profile));
    EXPECT_NEAR(profile.at(CARTA::StatsType::Extrema).at(0), 0.93505, 1e-5);
    EXPECT_NEAR(profile.at(CARTA::StatsType::Extrema).at(1), -1.47389, 1e-5);
    ExpectProfileIsDerivedFromItsTotals(Hdf5Images() / "10x10x2_nans.hdf5", 2, 6, 7, 2, 1);
}

// Region statistics are made by casacore rather than from totals, and their extrema is the same rule.
TEST_F(ReportedStatisticsTest, RegionStatisticsExtremaIsTheExtremeOfLargerMagnitude) {
    OpenedImage image(Hdf5Images() / "10x10x2_nans.hdf5");
    ASSERT_TRUE(image.IsValid());
    auto pixels = image.loader->GetImage();
    ASSERT_NE(pixels, nullptr);

    const auto extrema_of = [&](int x_min, int x_count, int y, int z) {
        const casacore::Slicer box(casacore::IPosition(3, x_min, y, z), casacore::IPosition(3, x_count, 1, 1));
        casacore::SubImage<float> region(*pixels, casacore::LCBox(box, pixels->shape()), false);
        std::map<CARTA::StatsType, std::vector<double>> stats;
        EXPECT_TRUE(CalcStatsValues(stats, {CARTA::StatsType::Extrema}, region, false));
        return stats[CARTA::StatsType::Extrema].at(0);
    };
    // -0.661528, 0.93505, 0.0490546 and 2.00239: the largest outweighs the smallest.
    EXPECT_NEAR(extrema_of(6, 4, 7, 0), 2.00239, 1e-5);
    // -1.47389 and 1.02885: the smallest outweighs the largest, and is a fraction.
    EXPECT_NEAR(extrema_of(6, 2, 7, 1), -1.47389, 1e-5);
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

// The module the loaders make them with. Nothing here needs a file: the interface is the totals.

namespace {

Totals TotalsOf(const CountedTotals& counted) {
    return {counted.num_pixels, counted.sum, counted.sum_sq, counted.min, counted.max, counted.sum_sq_dev};
}

void ExpectSameAsWrittenOut(const DerivedStatistics& derived, const Reported& expected, const std::string& where) {
    ExpectSame(derived.mean, expected.mean, where + " mean");
    ExpectSame(derived.rms, expected.rms, where + " rms");
    ExpectSame(derived.sigma, expected.sigma, where + " sigma");
    ExpectSame(derived.extrema, expected.extrema, where + " extrema");
}

} // namespace

class DerivedStatisticsTest : public ::testing::Test {};

TEST_F(DerivedStatisticsTest, TwoPixelsComeToWhatOneWorksOutByHand) {
    // The pixels 1 and 3.
    const auto derived = DeriveStatistics({2.0, 4.0, 10.0, 1.0, 3.0}, LonePixelSigma::nan);
    EXPECT_EQ(derived.mean, 2.0);
    EXPECT_EQ(derived.rms, std::sqrt(5.0));
    EXPECT_EQ(derived.sigma, std::sqrt(2.0));
    EXPECT_EQ(derived.extrema, 3.0);
}

TEST_F(DerivedStatisticsTest, NoValidPixelLeavesNothingToDeriveWhateverElseIsSaid) {
    for (const auto policy : {LonePixelSigma::zero, LonePixelSigma::nan}) {
        // A caller that never zeroed its sums, and one that never reset its extremes.
        for (const auto& totals : {Totals{}, Totals{0.0, 12.5, 30.0, -4.0, 9.0},
                 Totals{0.0, 0.0, 0.0, std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest()}}) {
            const auto derived = DeriveStatistics(totals, policy);
            EXPECT_TRUE(std::isnan(derived.mean));
            EXPECT_TRUE(std::isnan(derived.rms));
            EXPECT_TRUE(std::isnan(derived.sigma));
            EXPECT_TRUE(std::isnan(derived.extrema));
        }
    }
}

TEST_F(DerivedStatisticsTest, ACountThatIsNotACountLeavesNothingToDeriveEither) {
    for (const double count : {kNaN, -1.0}) {
        const auto derived = DeriveStatistics({count, 3.0, 9.0, 3.0, 3.0}, LonePixelSigma::zero);
        EXPECT_TRUE(std::isnan(derived.mean)) << count;
        EXPECT_TRUE(std::isnan(derived.sigma)) << count;
    }
}

TEST_F(DerivedStatisticsTest, OnePixelIsTheConsumersPolicyForSigmaAndNothingElse) {
    // The pixel -2.5.
    const Totals lone{1.0, -2.5, 6.25, -2.5, -2.5};
    const auto zero = DeriveStatistics(lone, LonePixelSigma::zero);
    const auto nan = DeriveStatistics(lone, LonePixelSigma::nan);

    EXPECT_EQ(zero.sigma, 0.0);
    EXPECT_TRUE(std::isnan(nan.sigma));
    for (const auto& derived : {zero, nan}) {
        EXPECT_EQ(derived.mean, -2.5);
        EXPECT_EQ(derived.rms, 2.5);
        EXPECT_EQ(derived.extrema, -2.5);
    }
}

TEST_F(DerivedStatisticsTest, TheSecondPixelIsWhereSigmaStopsBeingAPolicy) {
    // Two pixels that are equal have a spread of zero, which is a fact about them and not a choice.
    for (const auto policy : {LonePixelSigma::zero, LonePixelSigma::nan}) {
        const auto derived = DeriveStatistics({2.0, 6.0, 18.0, 3.0, 3.0}, policy);
        EXPECT_EQ(derived.sigma, 0.0);
    }
}

TEST_F(DerivedStatisticsTest, PixelsThatAreAllOneLargeValueHaveNoSpread) {
    // A 1000 x 1000 plane of 1e8: the sum of squares comes out a rounding error below the square of
    // the sum over the count, which the spread does not depend on. Without one, sigma is what the
    // sums make of it, as it always was: the square root of a negative number.
    const std::vector<float> pixels(1000 * 1000, 1.0e8f);
    const auto counted = CountPixels(pixels);
    ASSERT_LT(counted.sum_sq - counted.sum * counted.sum / counted.num_pixels, 0.0);
    for (const auto policy : {LonePixelSigma::zero, LonePixelSigma::nan}) {
        EXPECT_EQ(DeriveStatistics(TotalsOf(WithSpread(counted, pixels)), policy).sigma, 0.0);
        EXPECT_TRUE(std::isnan(DeriveStatistics(TotalsOf(counted), policy).sigma));
    }
}

TEST_F(DerivedStatisticsTest, ExtremaIsTheExtremeOfLargerMagnitudeAndTheLargestWhenTheyTie) {
    struct Case {
        double min;
        double max;
        double expected;
    };
    // Fractions among them, which is what an integer absolute value would get wrong.
    for (const auto& c : {Case{-3.0, 2.0, -3.0}, Case{-2.0, 3.0, 3.0}, Case{-2.0, 2.0, 2.0}, Case{-5.0, -1.0, -5.0}, Case{1.0, 4.0, 4.0},
             Case{0.0, 0.0, 0.0}, Case{-0.75, 0.5, -0.75}, Case{-0.5, 0.75, 0.75}, Case{-0.75, 0.75, 0.75}}) {
        const auto derived = DeriveStatistics({4.0, 1.0, 1.0, c.min, c.max}, LonePixelSigma::nan);
        EXPECT_EQ(derived.extrema, c.expected) << "min=" << c.min << " max=" << c.max;
        EXPECT_EQ(Extrema(c.min, c.max), c.expected) << "min=" << c.min << " max=" << c.max;
    }
}

TEST_F(DerivedStatisticsTest, TheSpreadAndTheMeanAreMadeOfTheSumsAlone) {
    const auto narrow = DeriveStatistics({5.0, 10.0, 30.0, 1.0, 3.0}, LonePixelSigma::nan);
    const auto wide = DeriveStatistics({5.0, 10.0, 30.0, -100.0, 100.0}, LonePixelSigma::nan);
    EXPECT_EQ(narrow.mean, wide.mean);
    EXPECT_EQ(narrow.rms, wide.rms);
    EXPECT_EQ(narrow.sigma, wide.sigma);
}

// The formulas as they were written out where a loader made them itself, against every kind of
// totals that a set of pixels comes to: none, one, a few and many, close to zero and far from it,
// and with pixels that are not finite among them.
TEST_F(DerivedStatisticsTest, MakesTheSameBitsAsTheFormulasItReplaces) {
    std::mt19937 random(20260930);
    std::uniform_real_distribution<float> spread(-1.0f, 1.0f);
    std::uniform_int_distribution<int> one_in_seven(0, 6);

    for (const std::size_t size : {0u, 1u, 2u, 3u, 10u, 101u, 5000u}) {
        for (const float offset : {0.0f, 3.0f, -1000.0f, 5.0e4f}) {
            std::vector<float> pixels(size);
            for (auto& pixel : pixels) {
                pixel = offset + spread(random);
                if (one_in_seven(random) == 0) {
                    pixel = std::numeric_limits<float>::quiet_NaN();
                }
            }
            const auto counted = CountPixels(pixels);
            const std::string where = "size=" + std::to_string(size) + " offset=" + std::to_string(offset);

            for (const auto& totals : {counted, WithSpread(counted, pixels)}) {
                const auto said = where + (totals.sum_sq_dev ? " with the spread" : " from the sums");
                ExpectSameAsWrittenOut(DeriveStatistics(TotalsOf(totals), LonePixelSigma::zero), ReportedFrom(totals, 0.0), said + " zero");
                ExpectSameAsWrittenOut(DeriveStatistics(TotalsOf(totals), LonePixelSigma::nan), ReportedFrom(totals, kNaN), said + " nan");
            }
        }
    }
}

// The statistics of a plane that the calculators make, which say NaN for one pixel.

namespace {

CountedTotals TotalsReportedBy(const BasicStats<float>& stats) {
    CountedTotals counted;
    counted.num_pixels = static_cast<double>(stats.num_pixels);
    counted.sum = stats.sum;
    counted.sum_sq = stats.sumSq;
    counted.min = stats.min_val;
    counted.max = stats.max_val;
    counted.sum_sq_dev = stats.sumSqDev;
    return counted;
}

void ExpectDerivedFromItsOwnTotals(const BasicStats<float>& stats, const std::string& where) {
    const auto expected = ReportedFrom(TotalsReportedBy(stats), kNaN);
    ExpectSame(stats.mean, expected.mean, where + " mean");
    ExpectSame(stats.rms, expected.rms, where + " rms");
    ExpectSame(stats.stdDev, expected.sigma, where + " sigma");
}

std::vector<float> SomePixels(std::size_t size, std::mt19937& random) {
    std::uniform_real_distribution<float> spread(-1.0f, 1.0f);
    std::uniform_int_distribution<int> one_in_five(0, 4);
    std::vector<float> pixels(size);
    for (auto& pixel : pixels) {
        pixel = 2.0f + spread(random);
        if (one_in_five(random) == 0) {
            pixel = std::numeric_limits<float>::quiet_NaN();
        }
    }
    return pixels;
}

} // namespace

class BasicStatsDerivedTest : public ::testing::Test {};

TEST_F(BasicStatsDerivedTest, ACalculatedPlaneIsDerivedFromItsOwnTotals) {
    std::mt19937 random(7);
    for (const std::size_t size : {0u, 1u, 2u, 3u, 10u, 400u}) {
        const auto pixels = SomePixels(size, random);
        BasicStatsCalculator<float> calculator(pixels.data(), pixels.size());
        calculator.reduce();
        const auto stats = calculator.GetStats();
        ExpectDerivedFromItsOwnTotals(stats, "size=" + std::to_string(size));
    }
}

TEST_F(BasicStatsDerivedTest, APlaneWithOneValidPixelHasNoSigma) {
    const std::vector<float> pixels = {std::numeric_limits<float>::quiet_NaN(), 1.5f, std::numeric_limits<float>::infinity()};
    BasicStatsCalculator<float> calculator(pixels.data(), pixels.size());
    calculator.reduce();
    const auto stats = calculator.GetStats();
    ASSERT_EQ(stats.num_pixels, 1u);
    EXPECT_EQ(stats.mean, 1.5);
    EXPECT_TRUE(std::isnan(stats.stdDev));
}

TEST_F(BasicStatsDerivedTest, APlaneWithNoValidPixelIsUndefined) {
    const std::vector<float> pixels = {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()};
    BasicStatsCalculator<float> calculator(pixels.data(), pixels.size());
    calculator.reduce();
    const auto stats = calculator.GetStats();
    ASSERT_EQ(stats.num_pixels, 0u);
    EXPECT_TRUE(std::isnan(stats.mean));
    EXPECT_TRUE(std::isnan(stats.stdDev));
    EXPECT_TRUE(std::isnan(stats.rms));
}

TEST_F(BasicStatsDerivedTest, StatisticsOfNoPixelsYetHaveNothingDerived) {
    const BasicStats<float> nothing;
    EXPECT_EQ(nothing.num_pixels, 0u);
    EXPECT_EQ(nothing.sum, 0.0);
    EXPECT_EQ(nothing.sumSq, 0.0);
    EXPECT_TRUE(std::isnan(nothing.mean));
    EXPECT_TRUE(std::isnan(nothing.stdDev));
    EXPECT_TRUE(std::isnan(nothing.rms));
    // The extremes stay the identities a join starts from.
    EXPECT_EQ(nothing.min_val, std::numeric_limits<float>::max());
    EXPECT_EQ(nothing.max_val, std::numeric_limits<float>::lowest());
}

// A cube of planes with no valid pixel says what each of its planes says.
TEST_F(BasicStatsDerivedTest, PlanesWithNoValidPixelJoinedAreUndefinedAsEachOfThemIs) {
    const std::vector<float> pixels = {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()};
    BasicStatsCalculator<float> calculator(pixels.data(), pixels.size());
    calculator.reduce();
    const auto plane = calculator.GetStats();

    BasicStats<float> joined;
    joined.join(plane);
    joined.join(plane);
    ASSERT_EQ(joined.num_pixels, 0u);
    for (const auto& [name, value] : std::map<std::string, std::pair<double, double>>{
             {"mean", {joined.mean, plane.mean}}, {"stdDev", {joined.stdDev, plane.stdDev}}, {"rms", {joined.rms, plane.rms}}}) {
        EXPECT_TRUE(std::isnan(value.first)) << name << " of the cube is " << value.first;
        EXPECT_TRUE(std::isnan(value.second)) << name << " of a plane is " << value.second;
    }
}

TEST_F(BasicStatsDerivedTest, PlanesJoinedAreDerivedFromTheirTotalsAsTheyGrow) {
    std::mt19937 random(11);
    BasicStats<float> joined;
    // Chunks that are one pixel, none, and more: the sigma is undefined until the second pixel arrives.
    for (const std::size_t size : {1u, 0u, 1u, 6u, 0u, 30u}) {
        const auto pixels = SomePixels(size, random);
        BasicStatsCalculator<float> calculator(pixels.data(), pixels.size());
        calculator.reduce();
        const auto chunk = calculator.GetStats();
        joined.join(chunk);
        if (joined.num_pixels > 0) {
            ExpectDerivedFromItsOwnTotals(joined, "after " + std::to_string(joined.num_pixels) + " pixels");
        }
    }
    EXPECT_GT(joined.num_pixels, 1u);
}

// A cube's statistics are joined from its planes'. The Zarr loader's planes carry their spread, and
// the cube's is theirs put together; any other loader's carry none, and the cube's sigma is made from
// the sums as each plane's was.

namespace {

// About normal, of unit variance, the same every run.
std::vector<float> PixelsAround(double centre, double spread, std::size_t size, unsigned seed) {
    std::mt19937 random(seed);
    std::uniform_real_distribution<double> uniform(0.0, 1.0);
    std::vector<float> pixels(size);
    for (auto& pixel : pixels) {
        const double noise = (uniform(random) + uniform(random) + uniform(random) + uniform(random) - 2.0) * std::sqrt(3.0);
        pixel = static_cast<float>(centre + (spread * noise));
    }
    return pixels;
}

// A plane's statistics as the Zarr loader reports them: the calculator's, with the spread beside.
BasicStats<float> WithItsSpread(const std::vector<float>& pixels) {
    BasicStatsCalculator<float> calculator(pixels.data(), pixels.size());
    calculator.reduce();
    auto stats = calculator.GetStats();
    stats.sumSqDev = WithSpread(CountPixels(pixels), pixels).sum_sq_dev;
    return stats;
}

} // namespace

TEST_F(BasicStatsDerivedTest, PlanesWithTheirSpreadsJoinedHaveTheCubesSpread) {
    // Far from zero against their spread, where the sums cannot say it: 1e7 with a spread of 0.5.
    std::vector<float> cube;
    BasicStats<float> joined;
    for (unsigned z = 0; z < 10; ++z) {
        const auto plane = PixelsAround(1.0e7, 0.5, 100000, z);
        cube.insert(cube.end(), plane.begin(), plane.end());
        joined.join(WithItsSpread(plane));
    }
    const auto expected = WithSpread(CountPixels(cube), cube);
    ASSERT_TRUE(joined.sumSqDev.has_value());
    EXPECT_NEAR(*joined.sumSqDev, *expected.sum_sq_dev, 1e-10 * *expected.sum_sq_dev);
    ExpectDerivedFromItsOwnTotals(joined, "ten planes with their spreads");
    // 0.576 rather than 0.5, since a float holds 1e7 only to the nearest unit; the sums say 1.48.
    const double own = ReportedFrom(expected, kNaN).sigma;
    EXPECT_NEAR(joined.stdDev, own, 1e-10 * own) << "the sums of these planes say " << ReportedFrom(CountPixels(cube), kNaN).sigma;
}

TEST_F(BasicStatsDerivedTest, APlaneWithoutASpreadLeavesTheCubeWithout) {
    const auto first = PixelsAround(3.0, 1.0, 1000, 1);
    const auto second = PixelsAround(3.0, 1.0, 1000, 2);
    BasicStatsCalculator<float> calculator(second.data(), second.size());
    calculator.reduce();
    const auto without = calculator.GetStats();
    ASSERT_FALSE(without.sumSqDev.has_value()) << "only the Zarr loader counts the spread";

    BasicStats<float> joined;
    joined.join(WithItsSpread(first));
    ASSERT_TRUE(joined.sumSqDev.has_value());
    joined.join(without);
    EXPECT_FALSE(joined.sumSqDev.has_value()) << "half a spread is no spread";
    ExpectDerivedFromItsOwnTotals(joined, "a plane with a spread and one without");
}

// A region profile's sigma, from the spread a channel carries when its reader counted one.
TEST_F(BasicStatsDerivedTest, AProfileChannelWithASpreadHasItsSigma) {
    const auto pixels = PixelsAround(1.0e7, 0.5, 10000, 3);
    const auto counted = WithSpread(CountPixels(pixels), pixels);
    ChannelTotals channel;
    channel.read = true;
    channel.num_pixels = counted.num_pixels;
    channel.sum = counted.sum;
    channel.sum_sq = counted.sum_sq;
    channel.min = counted.min;
    channel.max = counted.max;
    channel.sum_sq_dev = counted.sum_sq_dev;
    ChannelTotals without = channel;
    without.sum_sq_dev.reset();

    const auto stats = RegionProfileStatistics({channel, without}, kNaN);
    ExpectSame(stats.at(CARTA::StatsType::Sigma).at(0), ReportedFrom(counted, 0.0).sigma, "with the spread");
    CountedTotals sums_only = counted;
    sums_only.sum_sq_dev.reset();
    ExpectSame(stats.at(CARTA::StatsType::Sigma).at(1), ReportedFrom(sums_only, 0.0).sigma, "from the sums");
}

// A file that keeps statistics for no valid pixel and for one, which none of the fixtures does.
//
// The statistics stored in the file are all a loader has to go on -- it never sees the pixels -- so
// they are rewritten here on a copy of a fixture, and what it reports is what it makes of them.

namespace {

struct StoredTotals {
    double nan_count;
    double sum;
    double sum_sq;
    double min;
    double max;
};

// What a plane or a cube with no valid pixel is stored as: the count of every pixel as not a number,
// and the extremes left as the identities they started from.
StoredTotals NoValidPixel(double pixels) {
    return {pixels, 0.0, 0.0, std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest()};
}

// One valid pixel, of value 0.5, which a float holds exactly along with its square.
StoredTotals ThePixelHalf(double pixels) {
    return {pixels - 1.0, 0.5, 0.25, 0.5, 0.5};
}

void StoreTotals(H5::H5File& file, const std::string& group, const std::vector<StoredTotals>& totals) {
    const auto store = [&](const std::string& name, double StoredTotals::* member) {
        std::vector<double> values;
        for (const auto& one : totals) {
            values.push_back(one.*member);
        }
        file.openDataSet("/0/Statistics/" + group + "/" + name).write(values.data(), H5::PredType::NATIVE_DOUBLE);
    };
    store("NAN_COUNT", &StoredTotals::nan_count);
    store("SUM", &StoredTotals::sum);
    store("SUM_SQ", &StoredTotals::sum_sq);
    store("MIN", &StoredTotals::min);
    store("MAX", &StoredTotals::max);
}

// A copy of 10x10x2_nans.hdf5, whose two planes and whose cube say what they are given.
fs::path FileStoring(const std::string& name, const StoredTotals& first_plane, const StoredTotals& second_plane, const StoredTotals& cube) {
    const auto copy = TestRoot() / "data" / "generated" / name;
    fs::copy_file(Hdf5Images() / "10x10x2_nans.hdf5", copy, fs::copy_options::overwrite_existing);
    H5::H5File file(copy.string(), H5F_ACC_RDWR);
    StoreTotals(file, "XY", {first_plane, second_plane});
    StoreTotals(file, "XYZ", {cube});
    return copy;
}

} // namespace

class StoredStatsAtTheEdgeTest : public ::testing::Test {};

TEST_F(StoredStatsAtTheEdgeTest, APlaneWithNoValidPixelHasNothingToDerive) {
    const auto path = FileStoring("stored_no_valid_pixel_in_a_plane.hdf5", NoValidPixel(100), ThePixelHalf(100), NoValidPixel(200));
    StoredStats stored(path);
    ASSERT_TRUE(stored.IsValid());
    const auto empty = stored.At(0);

    EXPECT_EQ(empty.at(CARTA::StatsType::NumPixels), 0.0);
    for (const auto stat : {CARTA::StatsType::Mean, CARTA::StatsType::RMS, CARTA::StatsType::Sigma, CARTA::StatsType::Extrema}) {
        EXPECT_TRUE(std::isnan(empty.at(stat))) << "stat=" << static_cast<int>(stat);
    }
}

TEST_F(StoredStatsAtTheEdgeTest, APlaneWithOneValidPixelHasNoSigma) {
    const auto path = FileStoring("stored_one_valid_pixel_in_a_plane.hdf5", NoValidPixel(100), ThePixelHalf(100), NoValidPixel(200));
    StoredStats stored(path);
    ASSERT_TRUE(stored.IsValid());
    const auto lone = stored.At(1);

    EXPECT_EQ(lone.at(CARTA::StatsType::NumPixels), 1.0);
    EXPECT_EQ(lone.at(CARTA::StatsType::Mean), 0.5);
    EXPECT_EQ(lone.at(CARTA::StatsType::RMS), 0.5);
    EXPECT_EQ(lone.at(CARTA::StatsType::Extrema), 0.5);
    EXPECT_TRUE(std::isnan(lone.at(CARTA::StatsType::Sigma))) << "a plane says NaN, unlike a profile";
}

TEST_F(StoredStatsAtTheEdgeTest, ACubeWithNoValidPixelHasNothingToDerive) {
    const auto path = FileStoring("stored_no_valid_pixel_in_a_cube.hdf5", NoValidPixel(100), NoValidPixel(100), NoValidPixel(200));
    StoredStats stored(path);
    ASSERT_TRUE(stored.IsValid());
    const auto empty = stored.At(-1);

    EXPECT_EQ(empty.at(CARTA::StatsType::NumPixels), 0.0);
    for (const auto stat : {CARTA::StatsType::Mean, CARTA::StatsType::RMS, CARTA::StatsType::Sigma, CARTA::StatsType::Extrema}) {
        EXPECT_TRUE(std::isnan(empty.at(stat))) << "stat=" << static_cast<int>(stat);
    }
}

TEST_F(StoredStatsAtTheEdgeTest, ACubeWithOneValidPixelHasNoSigma) {
    const auto path = FileStoring("stored_one_valid_pixel_in_a_cube.hdf5", NoValidPixel(100), NoValidPixel(100), ThePixelHalf(200));
    StoredStats stored(path);
    ASSERT_TRUE(stored.IsValid());
    const auto lone = stored.At(-1);

    EXPECT_EQ(lone.at(CARTA::StatsType::NumPixels), 1.0);
    EXPECT_EQ(lone.at(CARTA::StatsType::Mean), 0.5);
    EXPECT_EQ(lone.at(CARTA::StatsType::RMS), 0.5);
    EXPECT_EQ(lone.at(CARTA::StatsType::Extrema), 0.5);
    EXPECT_TRUE(std::isnan(lone.at(CARTA::StatsType::Sigma)));
}
