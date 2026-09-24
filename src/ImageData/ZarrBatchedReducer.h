/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_ZARRBATCHEDREDUCER_H_
#define CARTA_SRC_IMAGEDATA_ZARRBATCHEDREDUCER_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include <casacore/casa/aipstype.h>

#include "ImageStats/BasicStatsCalculator.h"
#include "ImageStats/Histogram.h"
#include "Util/Image.h"

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

// One 2D region of a batched spectral reduction: its bounding box in image pixels, plus an
// optional raster mask laid out row-major with x fastest, exactly as casacore's LCRegionFixed
// stores one. Both the mask and this struct are borrowed for the duration of the call.
//
// A null mask selects the whole bounding box, and that is a required case rather than a shortcut:
// an unrotated rectangle becomes an LCBox, whose getMask() is empty.
struct RegionMaskSpec {
    std::uint64_t x_start = 0;
    std::uint64_t y_start = 0;
    std::uint64_t width = 0;
    std::uint64_t height = 0;
    const casacore::Bool* mask = nullptr;
};

// One run of channels of a batched reduction, for every region at once.
//
// NumPixels(r) and Sum(r) are region r's channel_count values, and point into the loader's own
// buffer: they are valid only until the callback returns. How the loader lays its regions out is
// its own business, which is why they are asked for by region rather than walked with a stride. A
// channel whose region caught no valid pixel has num_pixels zero, which is the caller's signal that
// the mean it wants does not exist rather than a number to divide by.
struct RegionSpectralBlock {
    std::size_t first_channel = 0;
    std::size_t channel_count = 0;
    std::size_t region_count = 0;
    // One pointer per region, filled by the loader and read through the two accessors.
    std::vector<const double*> num_pixels;
    std::vector<const double*> sums;

    const double* NumPixels(std::size_t region) const {
        return num_pixels.at(region);
    }
    const double* Sum(std::size_t region) const {
        return sums.at(region);
    }

    // Whether these values are final. A reduction whose block spans more than one read hands the
    // block over as it fills, so a caller has something to show long before the last pixel is in;
    // the same channels arrive again, refined, and a last time with this set. The counts and sums
    // of an unfinished block are honest over what has been read, but they are not the answer, so a
    // caller counting off finished channels must not count these.
    bool complete = true;
    // The fraction of this block's chunks that are in the values, in [0, 1]. One when complete.
    double completeness = 1.0;
};

// How a batched walk ended.
//
// `failed` covers every way of not answering -- a request the walk does not serve, such as an empty
// range or a stokes it cannot read, as well as a read that went wrong, which the loader logs -- and
// in each the caller keeps its own route. `cancelled` is the caller's own callback having said stop,
// which is not a reason to try another route. Kept apart because a bool could not keep them apart:
// it used to be false for both, and the caller had to watch its own callback to tell which.
enum class ZarrBatchOutcome { finished, cancelled, failed };

// What a Zarr loader can do beyond FileLoader: read the pixels once and answer many questions from
// them. The caller's own routes ask plane by plane or region by region, and each of those reads the
// same chunks again; these read each chunk once.
//
// Reached through FileLoader::ZarrBatched(), which is null for every other loader, so having one is
// what says the path exists and the outcome only says how it went. Named for Zarr because Zarr is the
// only format with one; nothing in the signatures would stop another from implementing it.
class ZarrBatchedReducer {
public:
    virtual ~ZarrBatchedReducer() = default;

    // Basic statistics for every plane of one stokes. `plane_callback` receives each plane as it is
    // finished; returning false from it cancels.
    virtual ZarrBatchOutcome PlaneStats(int stokes, const std::function<bool(int z, const BasicStats<float>&)>& plane_callback) = 0;

    // Bin counts for every plane of one stokes over a fixed range. `plane_callback` receives each
    // plane's bins as it is finished; returning false from it cancels. An empty or inverted range is
    // the caller's degenerate case and is not served.
    virtual ZarrBatchOutcome PlaneHistograms(int stokes, int num_bins, const HistogramBounds& bounds,
        const std::function<bool(int z, const std::vector<int>& bins)>& plane_callback) = 0;

    // One histogram of the whole cube and its statistics, finding the range as it bins: one pass
    // rather than two, at the cost of where the bin edges land. `spatial_sample` reads every nth
    // pixel along both spatial axes, one being every pixel. `progress` is told how far along the walk
    // is between reads; returning false from it cancels.
    virtual ZarrBatchOutcome CubeHistogram(int stokes, int num_bins, std::uint64_t spatial_sample, BasicStats<float>& stats,
        std::vector<int>& bins, const std::function<bool(const CubeHistogramUpdate&)>& progress) = 0;

    // Many regions reduced over the same channels, a run of channels at a time. A position-velocity
    // cut is one box per pixel along the line -- 5,792 of them across a 4096 pixel diagonal -- and they
    // overlap heavily, so asking for them one at a time reads the same chunks once per box. Returning
    // false from the sink cancels.
    virtual ZarrBatchOutcome RegionSpectra(const std::vector<RegionMaskSpec>& regions, const AxisRange& z_range, int stokes,
        const std::function<bool(const RegionSpectralBlock&)>& sink) = 0;
};

}  // namespace carta

#endif  // CARTA_SRC_IMAGEDATA_ZARRBATCHEDREDUCER_H_
