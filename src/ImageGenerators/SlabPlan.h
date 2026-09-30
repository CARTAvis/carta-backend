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
