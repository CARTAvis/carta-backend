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

    // How many times over the image is decoded, when the budget could not reach a whole unit: each
    // chunk is decoded once per slab that lands in it. NaN when the slab was not shaped to chunks.
    double StoreReads() const;
};

// The slab to read an image in, to collapse it along `collapse_axis`.
//
// `decode_unit` is what the image decodes together -- a chunk, or a tile -- as the image reports it;
// one that is no finer than the image itself, or does not fit it, says there is nothing to align to,
// and the slab is then a fixed byte budget's worth of whole lines. `pixel_bytes` is what a pixel
// costs to hold, its mask included. `memory_bytes` is the machine's memory, 0 when unknown.
SlabPlan PlanSlab(const casacore::IPosition& shape, const casacore::IPosition& decode_unit, unsigned collapse_axis, unsigned pixel_bytes,
    std::uint64_t memory_bytes);

} // namespace carta

#endif // CARTA_SRC_IMAGEGENERATORS_SLABPLAN_H_
