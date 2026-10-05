/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "CubeHistogramCalculator.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

#include <spdlog/spdlog.h>

namespace carta {

namespace {

// Where the second half of the bar starts: the statistics are the first half and the bins the second.
constexpr double kBinsBegin = 0.5;

// How the caller is watching, said in the two questions the calculation asks of it.
struct Watch {
    const CubeHistogramControl& control;

    bool Stopped() const {
        return control.cancellation_requested && control.cancellation_requested();
    }
    // False when the caller said stop.
    bool Report(const CubeHistogramProgress& update) const {
        return !control.progress || control.progress(update);
    }
};

// Asks the routes in turn, and stops at the first that does not decline. Declined if every one did.
//
// Which route answered is said at debug, because nothing else does: a calculation that took a
// different route for its bins than for its statistics reads exactly like one that did not.
template <typename Ask>
BatchOutcome AskInTurn(const std::vector<CubeRoute>& routes, const char* half, Ask&& ask) {
    for (const auto& route : routes) {
        const auto outcome = ask(*route.reducer);
        if (outcome != BatchOutcome::declined) {
            spdlog::debug("Cube histogram, {}: taken by the {} route", half, route.name);
            return outcome;
        }
    }
    return BatchOutcome::declined;
}

// What a half that did not finish means to the caller. Declined is not a case: the caller has taken
// the next route by then, and a half that every route declined is failed.
// A histogram of a whole pass's bins over `bounds`, or what CalcHistogram makes of a range with
// nothing in it -- the single bin over [0, 0] the two halves answer with, holding the pixels that are
// zero -- when the bounds are empty or inverted. One pass binned no data here to recount, so the bin
// is counted from the statistics: a range that is empty and not inverted is one value, which is in
// the bin only if it is zero.
//
// Made directly from the statistics, an all-NaN or fully flagged cube was a success over [FLT_MAX,
// -FLT_MAX] with a negative bin width, and a constant one bins of no width over [v, v].
Histogram BinnedOver(int num_bins, const HistogramBounds& bounds, const BasicStats<float>& stats, const std::vector<int>& bins) {
    if (bounds.Invalid<float>()) {
        Histogram empty(1, HistogramBounds(0, 0), nullptr, 0);
        const bool zero = stats.num_pixels > 0 && stats.min_val == 0.0f && stats.max_val == 0.0f;
        // Held at the most a bin counts, as Histogram holds its own.
        const auto count = std::min<std::int64_t>(static_cast<std::int64_t>(stats.num_pixels), std::numeric_limits<int>::max());
        empty.SetHistogramBins({zero ? static_cast<int>(count) : 0});
        return empty;
    }
    Histogram histogram(num_bins, bounds, nullptr, 0);
    histogram.SetHistogramBins(bins);
    return histogram;
}

CubeHistogramOutcome Unfinished(BatchOutcome outcome) {
    return outcome == BatchOutcome::cancelled ? CubeHistogramOutcome::cancelled : CubeHistogramOutcome::failed;
}

} // namespace

CubeHistogramCalculator::CubeHistogramCalculator(std::vector<CubeRoute> routes, std::size_t depth)
    : _routes(std::move(routes)), _depth(depth) {}

CubeHistogramOutcome CubeHistogramCalculator::Calculate(
    const CubeHistogramRequest& request, const CubeHistogramControl& control, CubeHistogram& result) const {
    // A route that can find the range and bin at the same time answers both halves at once, when the
    // method asks for it; exact is the default, and then the two halves are all there is.
    //
    // Not for a request that fixes the bounds. The walk finds the range as it goes and re-aggregates
    // onto what it found, so its counts belong to the data's own extremes and there is nowhere to
    // hand it a range it was not given. Labelling those counts with the requested edges would publish
    // counts of one thing as counts of another. The two halves have the range before they bin, so
    // they are the ones that can answer this.
    //
    // A request already cancelled starts neither. Each route asks again at every read, but one pass
    // could only ask through its progress callback, which a walk done in one read never calls.
    if (control.cancellation_requested && control.cancellation_requested()) {
        return CubeHistogramOutcome::cancelled;
    }
    if (request.method.one_pass && !request.config.fixed_bounds) {
        if (const auto outcome = OnePass(request, control, result)) {
            return *outcome;
        }
    }
    return TwoHalves(request, control, result);
}

std::optional<CubeHistogramOutcome> CubeHistogramCalculator::OnePass(
    const CubeHistogramRequest& request, const CubeHistogramControl& control, CubeHistogram& result) const {
    const Watch watch{control};
    const int num_bins = request.config.num_bins;

    BasicStats<float> stats;
    std::vector<int> bins;
    const auto asked = AskInTurn(_routes, "one pass", [&](CubeReducer& reducer) {
        return reducer.OnePassCubeHistogram(request.stokes, num_bins, request.method.spatial_sample, control.cancellation_requested,
            stats, bins, [&](const CubeHistogramUpdate& update) {
                if (watch.Stopped()) {
                    return false;
                }
                CubeHistogramProgress reported;
                // One pass, so the whole bar belongs to it rather than its second half.
                reported.progress = static_cast<float>(update.progress);
                // The histogram of what has been read so far, which is what the two halves show from
                // their halfway mark on. The bounds widen as the walk goes, so they come from the
                // snapshot rather than from the cube's, which are not known yet.
                reported.partial = [&](BasicStats<float>& partial_stats, Histogram& partial) {
                    std::vector<int> partial_bins;
                    update.snapshot(partial_stats, partial_bins);
                    if (partial_bins.empty() || partial_stats.num_pixels == 0) {
                        return false;
                    }
                    partial = BinnedOver(num_bins, HistogramBounds(partial_stats.min_val, partial_stats.max_val), partial_stats, partial_bins);
                    return true;
                };
                return watch.Report(reported);
            });
    });
    if (asked == BatchOutcome::declined) {
        return std::nullopt;
    }
    if (asked != BatchOutcome::finished) {
        return Unfinished(asked);
    }

    CubeHistogram made;
    made.stats = stats;
    made.histogram = BinnedOver(num_bins, request.config.GetBounds(made.stats), made.stats, bins);
    // A stop that arrived during the last read is not an answer to anyone.
    if (watch.Stopped()) {
        return CubeHistogramOutcome::cancelled;
    }
    result = std::move(made);
    return CubeHistogramOutcome::finished;
}

// Each half is asked of the routes afresh. They report progress and stop at the same points whichever
// route answers, because they share what one plane costs.
CubeHistogramOutcome CubeHistogramCalculator::TwoHalves(
    const CubeHistogramRequest& request, const CubeHistogramControl& control, CubeHistogram& result) const {
    const Watch watch{control};
    const int num_bins = request.config.num_bins;
    const std::size_t total_z(_depth * 2);

    BasicStats<float> stats;
    auto take_plane_stats = [&](int plane, const BasicStats<float>& plane_stats) {
        stats.join(plane_stats);
        if (watch.Stopped()) {
            return false;
        }
        float this_plane(plane);
        CubeHistogramProgress reported;
        reported.progress = this_plane / total_z;
        return watch.Report(reported);
    };
    const auto found = AskInTurn(_routes, "statistics", [&](CubeReducer& reducer) {
        return reducer.PlaneStats(request.stokes, control.cancellation_requested, take_plane_stats);
    });
    if (found != BatchOutcome::finished) {
        return Unfinished(found);
    }
    if (watch.Stopped()) {
        return CubeHistogramOutcome::cancelled;
    }

    const auto bounds = request.config.GetBounds(stats);
    CubeHistogramProgress halfway;
    halfway.progress = kBinsBegin;
    halfway.milestone = true;
    if (!watch.Report(halfway)) {
        return CubeHistogramOutcome::cancelled;
    }

    // From here there is a histogram to show: the planes binned so far, over the statistics of the
    // whole cube.
    Histogram histogram;
    bool have_histogram(false);
    auto take_plane_histogram = [&](int plane, const Histogram& plane_histogram) {
        if (!have_histogram) {
            histogram = plane_histogram;
            have_histogram = true;
        } else {
            histogram.Add(plane_histogram);
        }
        if (watch.Stopped()) {
            return false;
        }
        float this_plane(plane);
        CubeHistogramProgress reported;
        reported.progress = kBinsBegin + (this_plane / total_z);
        reported.partial = [&](BasicStats<float>& partial_stats, Histogram& partial) {
            partial_stats = stats;
            partial = histogram;
            return true;
        };
        return watch.Report(reported);
    };
    const auto binned = AskInTurn(_routes, "bins", [&](CubeReducer& reducer) {
        return reducer.PlaneHistograms(request.stokes, num_bins, bounds, control.cancellation_requested, take_plane_histogram);
    });
    if (binned != BatchOutcome::finished) {
        return Unfinished(binned);
    }
    if (watch.Stopped()) {
        return CubeHistogramOutcome::cancelled;
    }

    result.stats = stats;
    result.histogram = histogram;
    return CubeHistogramOutcome::finished;
}

} // namespace carta
