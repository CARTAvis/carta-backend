/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGESTATS_CUBEREDUCER_H_
#define CARTA_SRC_IMAGESTATS_CUBEREDUCER_H_

#include <cstdint>
#include <functional>
#include <vector>

#include "ImageStats/BasicStatsCalculator.h"
#include "ImageStats/BatchOutcome.h"
#include "ImageStats/Histogram.h"

namespace carta {

// What a one-pass cube histogram reports while it runs.
//
// The two-pass path sends a histogram of the planes it has binned so far on every progress update
// and the frontend re-renders from it. One pass has the same thing to offer at any moment, because
// it tracks the extremes exactly as it goes and so always knows the bin edges for what it has read.
//
// `snapshot` is what asks for it, and it is separate from `progress` because it is not free: it
// re-aggregates the walk's provisional histograms, which on a cube with thousands of reads would
// cost more than the binning if it happened on every one. A caller reporting on a timer asks only
// when it reports. `stats` and `bins` come back over the same bounds, and those widen as more of
// the cube arrives.
struct CubeHistogramUpdate {
    double progress = 0.0;
    std::function<void(BasicStats<float>& stats, std::vector<int>& bins)> snapshot;
};

// What can be asked of a cube as a whole, one Stokes at a time: the answers a cube histogram is made
// of. A loader that has a walk over its pixels implements it, and so does anything that answers the
// same questions another way; the caller asks each in turn and takes the first that does not decline.
// See BatchOutcome for what declining is, and CONTEXT.md for what a walk and a route are.
//
// Reached through FileLoader::CubeWalk(), which is null for a loader that has no walk. Only the Zarr
// loader has one today; nothing in the signatures is particular to Zarr.
class CubeReducer {
public:
    virtual ~CubeReducer() = default;

    // Basic statistics for every plane of one stokes. `plane_callback` receives each plane as it is
    // finished; returning false from it cancels.
    //
    // `cancellation_requested` is asked before and after every read the walk makes, and a yes
    // cancels too. It is not the callback's job because the callback is only reached when a plane is
    // finished, and a plane of a large image is finished only after every read of its chunk layer:
    // a stop that waited for the callback waited seconds. Empty asks nothing.
    virtual BatchOutcome PlaneStats(int stokes, const std::function<bool()>& cancellation_requested,
        const std::function<bool(int z, const BasicStats<float>&)>& plane_callback) = 0;

    // Bin counts for every plane of one stokes over a fixed range. `plane_callback` receives each
    // plane's bins as it is finished; returning false from it cancels, and so does a yes from
    // `cancellation_requested`, asked as PlaneStats asks it. An empty or inverted range is the
    // caller's degenerate case and is declined.
    virtual BatchOutcome PlaneHistograms(int stokes, int num_bins, const HistogramBounds& bounds,
        const std::function<bool()>& cancellation_requested,
        const std::function<bool(int z, const std::vector<int>& bins)>& plane_callback) = 0;

    // One histogram of the whole cube and its statistics, finding the range as it bins: one pass
    // rather than two, at the cost of where the bin edges land. `spatial_sample` reads every nth
    // pixel along both spatial axes, one being every pixel. `progress` is told how far along the walk
    // is between reads; returning false from it cancels.
    virtual BatchOutcome OnePassCubeHistogram(int stokes, int num_bins, std::uint64_t spatial_sample, BasicStats<float>& stats,
        std::vector<int>& bins, const std::function<bool(const CubeHistogramUpdate&)>& progress) = 0;
};

} // namespace carta

#endif // CARTA_SRC_IMAGESTATS_CUBEREDUCER_H_
