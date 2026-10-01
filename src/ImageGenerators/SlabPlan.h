/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEGENERATORS_SLABPLAN_H_
#define CARTA_SRC_IMAGEGENERATORS_SLABPLAN_H_

#include <cstdint>

#include <casacore/casa/Arrays/IPosition.h>

namespace carta {

// How a moment steps through an image: the slab it holds at a time (see Slab in CONTEXT.md), and the
// order the axes are stepped in.
struct SlabPlan {
    casacore::IPosition slab;
    casacore::IPosition axis_path;

    // Whether the slab was shaped to the image's chunks. Only then are the two sizes below known:
    // one chunk of display area by the whole moment axis, and what was spent on a slab instead.
    bool chunked = false;
    std::uint64_t unit_bytes = 0;
    std::uint64_t budget_bytes = 0;

    // How many times over the image is decoded, taken as the mean, over the units the image touches, of
    // the slabs that touch each. It counts no help from a cache of decoded chunks: one large enough to
    // keep a unit's whole depth between the slabs that share it decodes each once. NaN when the slab was
    // not shaped to what the image decodes together.
    double store_reads = 0.0;

    // What a cache of decoded chunks has to hold for each to be decoded once: every chunk one slab
    // touches, at the most, in pixels. Two neighbouring slabs share the chunks a boundary between them
    // cuts through, and which of a slab's chunks the next one needs again depends on the order they
    // were decoded in, so it is all of them. 0 when no chunk is touched twice, or the slab was not
    // shaped to what the image decodes together.
    std::uint64_t reuse_pixels = 0;
    // The most a moment spends on that cache: a sixteenth of the machine, as for the slab, without
    // the slab's ceiling, which is about the collapse rather than the memory.
    std::uint64_t cache_ceiling_bytes = 0;

    // The cache to hold while stepping through the image, for an image that decodes `decoded_pixel_bytes`
    // a pixel: what it needs, as far as the ceiling allows. Short of what it needs it still keeps some
    // of what the next slab wants: on 2048 x 2048 x 2000 chunked 512x512x1, a quarter of the need
    // (1 GiB) decoded the store 1.72 times over rather than 2.
    std::uint64_t CacheBytes(unsigned decoded_pixel_bytes) const;
};

// The slab to read an image in, to collapse it along `collapse_axis`.
//
// `decode_unit` is what the image decodes together -- a chunk, or a tile -- as the image reports it;
// one that is no finer than the image itself, or does not fit it, says there is nothing to align to,
// and the slab is then a fixed byte budget's worth of whole lines. `pixel_bytes` is what a pixel
// costs to hold, its mask included. `memory_bytes` is the machine's memory, 0 when unknown.
//
// `grid_origin`, when given, is where the image's corner lies in the grid of units -- a region's corner
// in the image it was cut from, whose chunks `decode_unit` then is. It changes only the count of what
// the slabs cost, since slabs stepped from a corner off the grid straddle its units: they are still
// stepped from the image's corner, which measured faster than cutting them on the grid.
SlabPlan PlanSlab(const casacore::IPosition& shape, const casacore::IPosition& decode_unit, unsigned collapse_axis, unsigned pixel_bytes,
    std::uint64_t memory_bytes, const casacore::IPosition& grid_origin = casacore::IPosition());

} // namespace carta

#endif // CARTA_SRC_IMAGEGENERATORS_SLABPLAN_H_
