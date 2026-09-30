/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGESTATS_CUBEHISTOGRAMCALCULATOR_H_
#define CARTA_SRC_IMAGESTATS_CUBEHISTOGRAMCALCULATOR_H_

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

#include "Cache/RequirementsCache.h"
#include "ImageStats/BasicStatsCalculator.h"
#include "ImageStats/CubeHistogramMethod.h"
#include "ImageStats/CubeReducer.h"
#include "ImageStats/Histogram.h"

namespace carta {

// How far a cube histogram has got, as its calculator tells its caller.
struct CubeHistogramProgress {
    // The fraction of the whole calculation that is done.
    float progress = 0.0;
    // The report between the two halves of an exact histogram, which has always been sent whether or
    // not the caller's reporting interval had elapsed, and has never restarted it.
    bool milestone = false;
    // The histogram binned so far and the statistics to show it with, filled only if this returns
    // true. Empty until there is something to show: the first of two halves has only found a range.
    // Not free on one pass, where it re-aggregates the walk's provisional histograms, so a caller
    // asks only when it is about to report. Valid only for the call it arrives with.
    std::function<bool(BasicStats<float>& stats, Histogram& histogram)> partial;
};

// How a calculation ended. There is no declined here: that is one route's answer to one half, and
// it is the calculator's job to take another route after it, so a caller never sees it.
//
// `failed` is a half that no route would serve, or one that began and did not finish. The second is
// not retried by another route: the next one reads the same pixels the same way, and would meet the
// same failure after part of an answer has already been reported. `cancelled` is the caller's own
// predicate or progress callback having said stop.
enum class CubeHistogramOutcome { finished, cancelled, failed };

// One way of making a half of a cube histogram, named for the log. See Route in CONTEXT.md. The
// reducer is never null and must outlive the calculation.
struct CubeRoute {
    const char* name = "";
    CubeReducer* reducer = nullptr;
};

// What to make.
struct CubeHistogramRequest {
    int stokes = 0;
    // The bins to make and whether the bounds are fixed. `config.num_bins` must be a count, not
    // AUTO_BIN_SIZE: the routes are handed it as it stands and decline a count of no bins.
    HistogramConfig config;
    // Whether one pass may be taken. It is taken only when the bounds are free too, since one pass
    // finds its own range and has nowhere to be given another.
    CubeHistogramMethod method;
};

// How the calculation is watched and stopped. Either may be empty, and neither is asked for anything
// once the calculation is over.
struct CubeHistogramControl {
    // Asked between the reads of a walk, between planes, and once more after the last read, so that
    // a stop that arrives during it is not reported as an answer.
    std::function<bool()> cancellation_requested;
    // Told how far along the calculation is. Returning false cancels.
    std::function<bool(const CubeHistogramProgress&)> progress;
};

// The cube histogram: the histogram of every pixel of one Stokes, and the statistics its bins were
// laid over.
struct CubeHistogram {
    BasicStats<float> stats;
    Histogram histogram;
};

// Makes the cube histogram of one Stokes from whichever routes will answer.
//
// It knows nothing of where the pixels are or of what is kept afterwards. The routes are asked in the
// order given, so a walk goes first and plane by plane last, and each half asks them afresh: the
// statistics take the first route that does not decline them, and then the bins take the first that
// does not decline theirs. A loader that walks the statistics may decline the bins -- a cube of one
// value has an empty range -- and the halves then take different routes.
//
// One pass has no halves. It is asked of the routes in turn as well, and if none serves it the
// calculation goes on as two halves.
//
// Nothing is kept here. The result is written only when the outcome is `finished`.
class CubeHistogramCalculator {
public:
    // `depth` is the number of planes, which the progress of two halves is a fraction of.
    CubeHistogramCalculator(std::vector<CubeRoute> routes, std::size_t depth);

    CubeHistogramOutcome Calculate(const CubeHistogramRequest& request, const CubeHistogramControl& control, CubeHistogram& result) const;

private:
    // One pass, if a route serves it: the outcome, or nothing when every route declined and the
    // calculation goes on as two halves.
    std::optional<CubeHistogramOutcome> OnePass(
        const CubeHistogramRequest& request, const CubeHistogramControl& control, CubeHistogram& result) const;
    CubeHistogramOutcome TwoHalves(const CubeHistogramRequest& request, const CubeHistogramControl& control, CubeHistogram& result) const;

    std::vector<CubeRoute> _routes;
    std::size_t _depth;
};

} // namespace carta

#endif // CARTA_SRC_IMAGESTATS_CUBEHISTOGRAMCALCULATOR_H_
