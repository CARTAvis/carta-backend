/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// A cube histogram is made of routes that may decline, fail or be stopped, and none of that needs a
// file: these routes answer from planes held in memory and do as each test says.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "ImageStats/CubeHistogramCalculator.h"
#include "ImageStats/StatsCalculator.h"

using namespace carta;

namespace {

using Planes = std::vector<std::vector<float>>;

// Four planes of six pixels, 0 to 23, with one that is not a number -- a flagged pixel, which is
// counted in no statistic and in no bin.
Planes SampleCube() {
    Planes planes(4, std::vector<float>(6));
    for (std::size_t z = 0; z < planes.size(); ++z) {
        for (std::size_t i = 0; i < planes[z].size(); ++i) {
            planes[z][i] = static_cast<float>((z * 6) + i);
        }
    }
    planes[2][3] = std::numeric_limits<float>::quiet_NaN();
    return planes;
}

// Three planes of one value, whose range is empty.
Planes ConstantCube() {
    return Planes(3, std::vector<float>(6, 5.0f));
}

std::vector<float> Flatten(const Planes& planes, std::size_t first = 0, std::size_t count = std::numeric_limits<std::size_t>::max()) {
    std::vector<float> data;
    for (std::size_t z = first; z < planes.size() && z - first < count; ++z) {
        data.insert(data.end(), planes[z].begin(), planes[z].end());
    }
    return data;
}

BasicStats<float> StatsOf(const std::vector<float>& data) {
    BasicStats<float> stats;
    CalcBasicStats(stats, data.data(), data.size());
    return stats;
}

// A route that answers from `planes`, and does as it is told.
//
// serve answers. decline refuses the half before it begins. fail hands over the first plane and then
// says the pixels could not be read, which is what a walk that fails partway does.
class FakeRoute : public CubeReducer {
public:
    enum class Mode { serve, decline, fail };

    explicit FakeRoute(const Planes& planes) : _planes(planes) {}

    Mode stats = Mode::serve;
    Mode bins = Mode::serve;
    Mode one_pass = Mode::serve;
    // Declines bins over an empty range, as the Zarr loader does: it cannot produce the single bin
    // over [0, 0] that a caller makes of that.
    bool declines_empty_range = false;
    // A one-pass snapshot with nothing in it, which a caller has nothing to show of.
    bool snapshots_are_empty = false;

    int stats_calls = 0;
    int bins_calls = 0;
    int one_pass_calls = 0;
    int stokes_seen = -1;

    BatchOutcome PlaneStats(int stokes, const std::function<bool()>& cancellation_requested,
        const std::function<bool(int z, const BasicStats<float>&)>& plane_callback) override {
        ++stats_calls;
        stokes_seen = stokes;
        if (stats == Mode::decline) {
            return BatchOutcome::declined;
        }
        for (std::size_t z = 0; z < _planes.size(); ++z) {
            if (cancellation_requested && cancellation_requested()) {
                return BatchOutcome::cancelled;
            }
            if (!plane_callback(static_cast<int>(z), StatsOf(_planes[z]))) {
                return BatchOutcome::cancelled;
            }
            if (stats == Mode::fail) {
                return BatchOutcome::failed;
            }
        }
        return BatchOutcome::finished;
    }

    BatchOutcome PlaneHistograms(int stokes, int num_bins, const HistogramBounds& bounds,
        const std::function<bool()>& cancellation_requested,
        const std::function<bool(int z, const Histogram& histogram)>& plane_callback) override {
        ++bins_calls;
        stokes_seen = stokes;
        if (bins == Mode::decline || (declines_empty_range && !(bounds.min < bounds.max))) {
            return BatchOutcome::declined;
        }
        for (std::size_t z = 0; z < _planes.size(); ++z) {
            if (cancellation_requested && cancellation_requested()) {
                return BatchOutcome::cancelled;
            }
            const auto plane = CalcHistogram(num_bins, bounds, _planes[z].data(), _planes[z].size());
            if (!plane_callback(static_cast<int>(z), plane)) {
                return BatchOutcome::cancelled;
            }
            if (bins == Mode::fail) {
                return BatchOutcome::failed;
            }
        }
        return BatchOutcome::finished;
    }

    // Reads one plane at a time, and tells the caller how far it has got before every read after the
    // first, as the real walk does.
    BatchOutcome OnePassCubeHistogram(int stokes, int num_bins, std::uint64_t /*spatial_sample*/, BasicStats<float>& stats_out,
        std::vector<int>& bins_out, const std::function<bool(const CubeHistogramUpdate&)>& progress) override {
        ++one_pass_calls;
        stokes_seen = stokes;
        if (one_pass == Mode::decline) {
            return BatchOutcome::declined;
        }
        if (one_pass == Mode::fail) {
            return BatchOutcome::failed;
        }
        for (std::size_t z = 1; z < _planes.size() && progress; ++z) {
            CubeHistogramUpdate update;
            update.progress = static_cast<double>(z) / static_cast<double>(_planes.size());
            update.snapshot = [this, z, num_bins](BasicStats<float>& partial_stats, std::vector<int>& partial_bins) {
                if (snapshots_are_empty) {
                    partial_bins.clear();
                    return;
                }
                const auto so_far = Flatten(_planes, 0, z);
                partial_stats = StatsOf(so_far);
                partial_bins = CalcHistogram(num_bins, HistogramBounds(partial_stats.min_val, partial_stats.max_val), so_far.data(),
                    so_far.size())
                                   .GetHistogramBins();
            };
            if (!progress(update)) {
                return BatchOutcome::cancelled;
            }
        }
        const auto all = Flatten(_planes);
        stats_out = StatsOf(all);
        bins_out = CalcHistogram(num_bins, HistogramBounds(stats_out.min_val, stats_out.max_val), all.data(), all.size()).GetHistogramBins();
        return BatchOutcome::finished;
    }

private:
    Planes _planes;
};

HistogramConfig FreeBounds(int num_bins) {
    HistogramConfig config;
    config.num_bins = num_bins;
    return config;
}

HistogramConfig FixedBounds(int num_bins, float min, float max) {
    HistogramConfig config = FreeBounds(num_bins);
    config.fixed_bounds = true;
    config.bounds = HistogramBounds(min, max);
    return config;
}

CubeHistogramRequest Request(const HistogramConfig& config, int stokes = 0) {
    CubeHistogramRequest request;
    request.stokes = stokes;
    request.config = config;
    return request;
}

CubeHistogramRequest OnePassRequest(const HistogramConfig& config) {
    auto request = Request(config);
    request.method.one_pass = true;
    return request;
}

// What the cube histogram of `planes` is, made with the same primitives a plane is made with.
CubeHistogram Expected(const Planes& planes, const HistogramConfig& config) {
    CubeHistogram expected;
    const auto all = Flatten(planes);
    expected.stats = StatsOf(all);
    expected.histogram = CalcHistogram(config.num_bins, config.GetBounds(expected.stats), all.data(), all.size());
    return expected;
}

void ExpectSameCubeHistogram(const CubeHistogram& actual, const CubeHistogram& expected) {
    EXPECT_EQ(actual.stats.num_pixels, expected.stats.num_pixels);
    EXPECT_FLOAT_EQ(actual.stats.min_val, expected.stats.min_val);
    EXPECT_FLOAT_EQ(actual.stats.max_val, expected.stats.max_val);
    EXPECT_NEAR(actual.stats.sum, expected.stats.sum, 1e-9 * (1.0 + std::abs(expected.stats.sum)));
    EXPECT_FLOAT_EQ(actual.histogram.GetMinVal(), expected.histogram.GetMinVal());
    EXPECT_FLOAT_EQ(actual.histogram.GetMaxVal(), expected.histogram.GetMaxVal());
    EXPECT_EQ(actual.histogram.GetHistogramBins(), expected.histogram.GetHistogramBins());
}

constexpr int kBins = 9;

// The routes a Frame offers a loader that has a walk, and one that has not: the walk first, plane by
// plane last.
struct Routes {
    explicit Routes(const Planes& planes) : walk(planes), plane_by_plane(planes) {
        // Plane by plane cannot make a cube histogram in one pass, whatever it is asked.
        plane_by_plane.one_pass = FakeRoute::Mode::decline;
    }

    CubeHistogramCalculator Calculator() {
        return CubeHistogramCalculator({{"walk", &walk}, {"plane by plane", &plane_by_plane}}, depth);
    }

    FakeRoute walk;
    FakeRoute plane_by_plane;
    std::size_t depth = 4;
};

} // namespace

// ----------------------------------------------------------------------------------------------
// Which route answers

TEST(CubeHistogramCalculatorTest, TheFirstRouteThatServesAnswersBothHalves) {
    Routes routes(SampleCube());
    CubeHistogram result;
    ASSERT_EQ(routes.Calculator().Calculate(Request(FreeBounds(kBins), 2), {}, result), CubeHistogramOutcome::finished);

    ExpectSameCubeHistogram(result, Expected(SampleCube(), FreeBounds(kBins)));
    EXPECT_EQ(routes.walk.stats_calls, 1);
    EXPECT_EQ(routes.walk.bins_calls, 1);
    EXPECT_EQ(routes.walk.one_pass_calls, 0) << "exact is two halves";
    EXPECT_EQ(routes.plane_by_plane.stats_calls + routes.plane_by_plane.bins_calls, 0) << "a route that served is not followed";
    EXPECT_EQ(routes.walk.stokes_seen, 2) << "the stokes asked for is the stokes read";
}

TEST(CubeHistogramCalculatorTest, EachHalfTakesItsOwnRoute) {
    Routes routes(SampleCube());
    routes.walk.bins = FakeRoute::Mode::decline;
    CubeHistogram result;
    ASSERT_EQ(routes.Calculator().Calculate(Request(FreeBounds(kBins)), {}, result), CubeHistogramOutcome::finished);

    ExpectSameCubeHistogram(result, Expected(SampleCube(), FreeBounds(kBins)));
    EXPECT_EQ(routes.walk.stats_calls, 1) << "the statistics were walked";
    EXPECT_EQ(routes.plane_by_plane.stats_calls, 0);
    EXPECT_EQ(routes.walk.bins_calls, 1) << "the bins were asked of the walk first";
    EXPECT_EQ(routes.plane_by_plane.bins_calls, 1) << "and made plane by plane once it declined";
}

// A cube of one value has an empty range, which the loader's walk cannot bin over. It can still find
// that range, so the statistics are its and only the bins are not: taking one route for the whole
// calculation would have thrown away what the walk had already found.
TEST(CubeHistogramCalculatorTest, ACubeOfOneValueHasItsBinsMadePlaneByPlane) {
    Routes routes(ConstantCube());
    routes.walk.declines_empty_range = true;
    routes.depth = 3;
    CubeHistogram result;
    ASSERT_EQ(routes.Calculator().Calculate(Request(FreeBounds(kBins)), {}, result), CubeHistogramOutcome::finished);

    ExpectSameCubeHistogram(result, Expected(ConstantCube(), FreeBounds(kBins)));
    EXPECT_FLOAT_EQ(result.stats.min_val, result.stats.max_val);
    EXPECT_EQ(routes.walk.stats_calls, 1);
    EXPECT_EQ(routes.plane_by_plane.stats_calls, 0);
    EXPECT_EQ(routes.walk.bins_calls, 1);
    EXPECT_EQ(routes.plane_by_plane.bins_calls, 1);
}

TEST(CubeHistogramCalculatorTest, BinsAreLaidOverTheBoundsTheConfigFixes) {
    Routes routes(SampleCube());
    const auto config = FixedBounds(kBins, 4.0f, 20.0f);
    CubeHistogram result;
    ASSERT_EQ(routes.Calculator().Calculate(Request(config), {}, result), CubeHistogramOutcome::finished);

    ExpectSameCubeHistogram(result, Expected(SampleCube(), config));
    EXPECT_FLOAT_EQ(result.histogram.GetMinVal(), 4.0f);
    EXPECT_FLOAT_EQ(result.histogram.GetMaxVal(), 20.0f);
    EXPECT_FLOAT_EQ(result.stats.min_val, 0.0f) << "the statistics are of the cube, not of the bounds";
}

// ----------------------------------------------------------------------------------------------
// Declining, and failing

TEST(CubeHistogramCalculatorTest, AHalfNoRouteServesFails) {
    for (const bool statistics : {true, false}) {
        Routes routes(SampleCube());
        (statistics ? routes.walk.stats : routes.walk.bins) = FakeRoute::Mode::decline;
        (statistics ? routes.plane_by_plane.stats : routes.plane_by_plane.bins) = FakeRoute::Mode::decline;
        CubeHistogram result;
        EXPECT_EQ(routes.Calculator().Calculate(Request(FreeBounds(kBins)), {}, result), CubeHistogramOutcome::failed)
            << (statistics ? "statistics" : "bins");
    }
}

// The next route would read the same pixels the same way, and meet the same failure after part of an
// answer has already been reported.
TEST(CubeHistogramCalculatorTest, ARouteThatFailsPartwayIsNotFollowedByAnother) {
    {
        Routes routes(SampleCube());
        routes.walk.stats = FakeRoute::Mode::fail;
        int reports = 0;
        CubeHistogramControl control;
        control.progress = [&](const CubeHistogramProgress&) {
            ++reports;
            return true;
        };
        CubeHistogram result;
        EXPECT_EQ(routes.Calculator().Calculate(Request(FreeBounds(kBins)), control, result), CubeHistogramOutcome::failed);
        EXPECT_EQ(reports, 1) << "the plane the walk handed over, and no plane read after it";
        EXPECT_EQ(routes.plane_by_plane.stats_calls, 0);
        EXPECT_EQ(routes.walk.bins_calls, 0) << "the second half is not begun";
    }
    {
        Routes routes(SampleCube());
        routes.walk.bins = FakeRoute::Mode::fail;
        CubeHistogram result;
        EXPECT_EQ(routes.Calculator().Calculate(Request(FreeBounds(kBins)), {}, result), CubeHistogramOutcome::failed);
        EXPECT_EQ(routes.walk.stats_calls, 1);
        EXPECT_EQ(routes.plane_by_plane.bins_calls, 0);
    }
}

// ----------------------------------------------------------------------------------------------
// One pass

TEST(CubeHistogramCalculatorTest, OnePassIsTakenWhenAskedForAndTheBoundsAreFree) {
    Routes routes(SampleCube());
    CubeHistogram result;
    ASSERT_EQ(routes.Calculator().Calculate(OnePassRequest(FreeBounds(kBins)), {}, result), CubeHistogramOutcome::finished);

    ExpectSameCubeHistogram(result, Expected(SampleCube(), FreeBounds(kBins)));
    EXPECT_EQ(routes.walk.one_pass_calls, 1);
    EXPECT_EQ(routes.walk.stats_calls + routes.walk.bins_calls, 0) << "one pass is not two halves";
    EXPECT_EQ(routes.plane_by_plane.one_pass_calls, 0) << "the route that served is not followed";
}

TEST(CubeHistogramCalculatorTest, OnePassIsNotTakenWhenTheBoundsAreFixed) {
    Routes routes(SampleCube());
    const auto config = FixedBounds(kBins, 4.0f, 20.0f);
    CubeHistogram result;
    ASSERT_EQ(routes.Calculator().Calculate(OnePassRequest(config), {}, result), CubeHistogramOutcome::finished);

    ExpectSameCubeHistogram(result, Expected(SampleCube(), config));
    EXPECT_EQ(routes.walk.one_pass_calls, 0) << "one pass finds its own range, and has nowhere to be given another";
    EXPECT_EQ(routes.walk.stats_calls, 1);
    EXPECT_EQ(routes.walk.bins_calls, 1);
}

TEST(CubeHistogramCalculatorTest, AnExactMethodNeverAsksForOnePass) {
    Routes routes(SampleCube());
    CubeHistogram result;
    ASSERT_EQ(routes.Calculator().Calculate(Request(FreeBounds(kBins)), {}, result), CubeHistogramOutcome::finished);
    EXPECT_EQ(routes.walk.one_pass_calls + routes.plane_by_plane.one_pass_calls, 0);
}

TEST(CubeHistogramCalculatorTest, OnePassNoRouteServesIsMadeOfTwoHalves) {
    Routes routes(SampleCube());
    routes.walk.one_pass = FakeRoute::Mode::decline;
    CubeHistogram result;
    ASSERT_EQ(routes.Calculator().Calculate(OnePassRequest(FreeBounds(kBins)), {}, result), CubeHistogramOutcome::finished);

    ExpectSameCubeHistogram(result, Expected(SampleCube(), FreeBounds(kBins)));
    EXPECT_EQ(routes.walk.one_pass_calls, 1);
    EXPECT_EQ(routes.plane_by_plane.one_pass_calls, 1) << "each route is asked in turn";
    EXPECT_EQ(routes.walk.stats_calls, 1);
    EXPECT_EQ(routes.walk.bins_calls, 1);
}

TEST(CubeHistogramCalculatorTest, OnePassThatFailsDoesNotBecomeTwoHalves) {
    Routes routes(SampleCube());
    routes.walk.one_pass = FakeRoute::Mode::fail;
    CubeHistogram result;
    EXPECT_EQ(routes.Calculator().Calculate(OnePassRequest(FreeBounds(kBins)), {}, result), CubeHistogramOutcome::failed);
    EXPECT_EQ(routes.walk.stats_calls + routes.walk.bins_calls + routes.plane_by_plane.stats_calls, 0);
}

// ----------------------------------------------------------------------------------------------
// Stopping

TEST(CubeHistogramCalculatorTest, APredicateThatSaysStopCancelsBeforeAnyRead) {
    Routes routes(SampleCube());
    CubeHistogramControl control;
    control.cancellation_requested = [] { return true; };
    CubeHistogram result;
    EXPECT_EQ(routes.Calculator().Calculate(Request(FreeBounds(kBins)), control, result), CubeHistogramOutcome::cancelled);
    EXPECT_EQ(routes.plane_by_plane.stats_calls + routes.plane_by_plane.bins_calls, 0) << "a stop is not a reason to try another route";
}

// A stop that arrives once the statistics are found is met in the second half, on the walk's first
// read, and not by asking for the bins and waiting for them.
TEST(CubeHistogramCalculatorTest, AStopBetweenTheHalvesCancelsTheSecond) {
    Routes routes(SampleCube());
    bool stop = false;
    CubeHistogramControl control;
    control.cancellation_requested = [&] { return stop; };
    control.progress = [&](const CubeHistogramProgress& update) {
        stop = stop || update.milestone;
        return true;
    };
    CubeHistogram result;
    EXPECT_EQ(routes.Calculator().Calculate(Request(FreeBounds(kBins)), control, result), CubeHistogramOutcome::cancelled);
    EXPECT_EQ(routes.plane_by_plane.bins_calls, 0);
}

TEST(CubeHistogramCalculatorTest, AProgressCallbackThatSaysStopCancels) {
    // Anywhere in the first half, at the halfway mark, and in the second half.
    for (const int stop_at : {1, 5, 7}) {
        Routes routes(SampleCube());
        int reports = 0;
        CubeHistogramControl control;
        control.progress = [&](const CubeHistogramProgress&) { return ++reports != stop_at; };
        CubeHistogram result;
        EXPECT_EQ(routes.Calculator().Calculate(Request(FreeBounds(kBins)), control, result), CubeHistogramOutcome::cancelled)
            << "stopped at report " << stop_at;
        EXPECT_EQ(reports, stop_at) << "nothing is reported after a stop";
        EXPECT_EQ(routes.plane_by_plane.stats_calls + routes.plane_by_plane.bins_calls, 0);
    }
    Routes routes(SampleCube());
    CubeHistogramControl control;
    control.progress = [](const CubeHistogramProgress&) { return false; };
    CubeHistogram result;
    EXPECT_EQ(routes.Calculator().Calculate(OnePassRequest(FreeBounds(kBins)), control, result), CubeHistogramOutcome::cancelled)
        << "one pass is stopped in the same way";
}

// The last read may finish before the stop is noticed. What it produced is an answer to a question
// nobody is asking any more, and it is not reported as one.
TEST(CubeHistogramCalculatorTest, AStopAfterTheLastReadIsStillACancel) {
    Routes routes(SampleCube());
    bool stop = false;
    int reports = 0;
    const int last_report = static_cast<int>((routes.depth * 2) + 1);
    CubeHistogramControl control;
    control.cancellation_requested = [&] { return stop; };
    control.progress = [&](const CubeHistogramProgress&) {
        stop = ++reports == last_report;
        return true;
    };
    CubeHistogram result;
    EXPECT_EQ(routes.Calculator().Calculate(Request(FreeBounds(kBins)), control, result), CubeHistogramOutcome::cancelled);
    EXPECT_EQ(reports, last_report) << "every plane of both halves was read";
}

// ----------------------------------------------------------------------------------------------
// What a caller is told while it waits

// The statistics are the first half of the bar with nothing to show, there is one report at the
// halfway mark, and the bins are the second half with the histogram so far.
TEST(CubeHistogramCalculatorTest, TwoHalvesReportTheirFractionsAndOneMilestone) {
    Routes routes(SampleCube());
    const auto config = FreeBounds(kBins);
    const auto expected = Expected(SampleCube(), config);

    struct Report {
        float progress;
        bool milestone;
        bool has_partial;
        bool partial_ok = false;
        BasicStats<float> partial_stats;
        Histogram partial;
    };
    std::vector<Report> reports;
    CubeHistogramControl control;
    control.progress = [&](const CubeHistogramProgress& update) {
        Report report{update.progress, update.milestone, static_cast<bool>(update.partial)};
        if (update.partial) {
            report.partial_ok = update.partial(report.partial_stats, report.partial);
        }
        reports.push_back(std::move(report));
        return true;
    };
    CubeHistogram result;
    ASSERT_EQ(routes.Calculator().Calculate(Request(config), control, result), CubeHistogramOutcome::finished);

    const std::size_t depth = routes.depth;
    ASSERT_EQ(reports.size(), (depth * 2) + 1);
    for (std::size_t z = 0; z < depth; ++z) {
        EXPECT_FLOAT_EQ(reports[z].progress, static_cast<float>(z) / static_cast<float>(depth * 2)) << "statistics, plane " << z;
        EXPECT_FALSE(reports[z].milestone);
        EXPECT_FALSE(reports[z].has_partial) << "the first half has only found a range";
    }
    EXPECT_FLOAT_EQ(reports[depth].progress, 0.5f);
    EXPECT_TRUE(reports[depth].milestone);
    for (std::size_t z = 0; z < depth; ++z) {
        const auto& report = reports[depth + 1 + z];
        EXPECT_FLOAT_EQ(report.progress, 0.5f + (static_cast<float>(z) / static_cast<float>(depth * 2))) << "bins, plane " << z;
        EXPECT_FALSE(report.milestone);
        ASSERT_TRUE(report.has_partial);
        ASSERT_TRUE(report.partial_ok);
        EXPECT_EQ(report.partial_stats.num_pixels, expected.stats.num_pixels) << "over the statistics of the whole cube";
        const auto so_far = Flatten(SampleCube(), 0, z + 1);
        EXPECT_EQ(report.partial.GetHistogramBins(),
            CalcHistogram(kBins, expected.histogram.GetBounds(), so_far.data(), so_far.size()).GetHistogramBins())
            << "and the planes binned so far";
    }
    EXPECT_EQ(std::count_if(reports.begin(), reports.end(), [](const Report& r) { return r.milestone; }), 1);
}

TEST(CubeHistogramCalculatorTest, OnePassReportsAsItGoesAndHasNoHalves) {
    Routes routes(SampleCube());
    struct Report {
        float progress;
        bool milestone;
        bool partial_ok;
        BasicStats<float> partial_stats;
        Histogram partial;
    };
    std::vector<Report> reports;
    CubeHistogramControl control;
    control.progress = [&](const CubeHistogramProgress& update) {
        Report report{update.progress, update.milestone, false, {}, {}};
        EXPECT_TRUE(static_cast<bool>(update.partial));
        report.partial_ok = update.partial && update.partial(report.partial_stats, report.partial);
        reports.push_back(std::move(report));
        return true;
    };
    CubeHistogram result;
    ASSERT_EQ(routes.Calculator().Calculate(OnePassRequest(FreeBounds(kBins)), control, result), CubeHistogramOutcome::finished);

    ASSERT_EQ(reports.size(), routes.depth - 1) << "before every read after the first";
    for (std::size_t i = 0; i < reports.size(); ++i) {
        const auto z = i + 1;
        EXPECT_FLOAT_EQ(reports[i].progress, static_cast<float>(z) / static_cast<float>(routes.depth));
        EXPECT_FALSE(reports[i].milestone) << "one pass has no halves to mark";
        ASSERT_TRUE(reports[i].partial_ok);
        const auto so_far = StatsOf(Flatten(SampleCube(), 0, z));
        EXPECT_FLOAT_EQ(reports[i].partial_stats.max_val, so_far.max_val) << "over the range found so far";
        EXPECT_FLOAT_EQ(reports[i].partial.GetMaxVal(), so_far.max_val);
    }
}

TEST(CubeHistogramCalculatorTest, ASnapshotWithNothingInItHasNothingToShow) {
    Routes routes(SampleCube());
    routes.walk.snapshots_are_empty = true;
    int reports = 0;
    CubeHistogramControl control;
    control.progress = [&](const CubeHistogramProgress& update) {
        ++reports;
        BasicStats<float> stats;
        Histogram histogram;
        EXPECT_FALSE(update.partial && update.partial(stats, histogram));
        return true;
    };
    CubeHistogram result;
    ASSERT_EQ(routes.Calculator().Calculate(OnePassRequest(FreeBounds(kBins)), control, result), CubeHistogramOutcome::finished);
    EXPECT_GT(reports, 0);
}

// ----------------------------------------------------------------------------------------------
// Keeping nothing

TEST(CubeHistogramCalculatorTest, TheResultIsLeftAloneUnlessTheOutcomeIsFinished) {
    Routes routes(SampleCube());
    routes.walk.bins = FakeRoute::Mode::fail;
    CubeHistogram result;
    result.stats.num_pixels = 12345;
    EXPECT_EQ(routes.Calculator().Calculate(Request(FreeBounds(kBins)), {}, result), CubeHistogramOutcome::failed);
    EXPECT_EQ(result.stats.num_pixels, 12345u) << "the statistics were found, and are not handed over without the bins";
    EXPECT_EQ(result.histogram.GetNbins(), 0u);
}
