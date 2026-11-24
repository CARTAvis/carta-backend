/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEGENERATORS_SLABPLAN_H_
#define CARTA_SRC_IMAGEGENERATORS_SLABPLAN_H_

#include <cstdint>

#include <casacore/casa/Arrays/IPosition.h>

namespace carta {

// How a moment steps through an image: the slab that it holds at a time, and the order in which it steps the axes.
struct SlabPlan {
    casacore::IPosition slab;
    casacore::IPosition axis_path;

    // True if the slab was shaped to the image's chunks. Only then are the two sizes below set. unit_bytes is one
    // chunk of display area by the whole moment axis, and budget_bytes is what a slab may use.
    bool chunked = false;
    std::uint64_t unit_bytes = 0;
    std::uint64_t budget_bytes = 0;

    // How many times each chunk is decoded on average, without a cache. NaN if the slab was not shaped to chunks.
    double store_reads = 0.0;

    // The pixels that a cache must keep for the next slab to find the chunks it shares with this one. This is every
    // chunk that one slab touches, because the decode order decides which chunks are shared. 0 if none is decoded twice.
    std::uint64_t reuse_pixels = 0;
    // The same count, in whole chunks.
    std::uint64_t reuse_chunks = 0;
    // store_reads with that cache. Along the axis stepped first, each chunk is decoded once. Along the other axes the
    // count does not change, because the slab that shared a chunk came a whole sweep earlier. NaN when store_reads is NaN.
    double cached_store_reads = 0.0;
    // The most that a moment spends on that cache: a sixteenth of the machine, at most 1 GiB and at least 64 MiB.
    std::uint64_t cache_ceiling_bytes = 0;

    // The cache to hold for an image whose pixels decode to `decoded_pixel_bytes` each: reuse_pixels, cut to the ceiling.
    // A smaller cache still saves some decodes. During the walk, resident memory can reach about a third more than the
    // cache plus the slab, because glibc's arenas keep what the decoding threads free.
    std::uint64_t CacheBytes(unsigned decoded_pixel_bytes) const;
    // The same, for an image that gives the size of one decoded chunk, at its stored type and with its flag chunks.
    // A cache counted in floats would hold only half the chunks of a float64 image.
    std::uint64_t CacheBytesOfChunks(std::uint64_t decoded_chunk_bytes) const;
};

// Plans the slab for collapsing an image along `collapse_axis`.
// `decode_unit` is what the image decodes at once, such as a chunk. If it is not finer than the image, or has the
// wrong number of axes, the slab is a fixed byte budget of whole lines.
// `pixel_bytes` is the cost of one pixel with its mask. `memory_bytes` is the machine's memory, or 0 when unknown.
// `grid_origin`, if given, is the image's corner in the grid of units. It changes only the counts in the plan.
// The slabs are still stepped from the image's corner, because that measured faster than cutting them on the grid.
// `result_bytes` is what the walk keeps for each line of a slab, such as one value and one mask per moment.
SlabPlan PlanSlab(const casacore::IPosition& shape, const casacore::IPosition& decode_unit, unsigned collapse_axis, unsigned pixel_bytes,
    std::uint64_t memory_bytes, const casacore::IPosition& grid_origin = casacore::IPosition(), std::uint64_t result_bytes = 0);

} // namespace carta

#endif // CARTA_SRC_IMAGEGENERATORS_SLABPLAN_H_
