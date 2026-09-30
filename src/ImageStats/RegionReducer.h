/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGESTATS_REGIONREDUCER_H_
#define CARTA_SRC_IMAGESTATS_REGIONREDUCER_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include <casacore/casa/aipstype.h>

#include "ImageStats/BatchOutcome.h"
#include "Util/Image.h"

namespace carta {

// One 2D region of a walk over many regions: its bounding box in image pixels, plus an
// optional raster mask laid out row-major with x fastest, exactly as casacore's LCRegionFixed
// stores one. Both the mask and this struct are borrowed for the duration of the call.
//
// A null mask selects the whole bounding box, for a caller that knows its region is exactly that.
// RegionHandler never passes one: every closed region reaches it as an LCPolygon with a raster,
// rotated or not, and one that arrived without a raster would be declined there rather than taken
// for its whole box.
struct RegionMaskSpec {
    std::uint64_t x_start = 0;
    std::uint64_t y_start = 0;
    std::uint64_t width = 0;
    std::uint64_t height = 0;
    const casacore::Bool* mask = nullptr;
};

// One run of channels of a walk over many regions, for every region at once.
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

// What a loader can do beyond FileLoader for many regions: read the pixels once and answer every
// region's question from them. The caller's own route asks region by region, and each of those reads
// the same chunks again; this reads each chunk once. The whole-cube half is CubeReducer.
//
// Reached through FileLoader::RegionWalk(), which is null for a loader that has no such walk. Only
// the Zarr loader has one today; nothing in the signature is particular to Zarr.
class RegionReducer {
public:
    virtual ~RegionReducer() = default;

    // Many regions reduced over the same channels, a run of channels at a time. A position-velocity
    // cut is one box per pixel along the line -- 5,792 of them across a 4096 pixel diagonal -- and they
    // overlap heavily, so asking for them one at a time reads the same chunks once per box. Returning
    // false from the sink cancels.
    virtual BatchOutcome RegionSpectra(const std::vector<RegionMaskSpec>& regions, const AxisRange& z_range, int stokes,
        const std::function<bool(const RegionSpectralBlock&)>& sink) = 0;
};

} // namespace carta

#endif // CARTA_SRC_IMAGESTATS_REGIONREDUCER_H_
