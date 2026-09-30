/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "SlabPlan.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace carta {

namespace {

// Whole lines along the collapse axis, as many as a fixed budget holds, filling the other axes in
// their natural order. For an image with no chunking to align to.
SlabPlan PlanByBytes(const casacore::IPosition& shape, unsigned collapse_axis, unsigned pixel_bytes) {
    // Arbitrary, but reasonable.
    static const std::uint64_t limit_bytes = 20000000;

    const unsigned ndim = shape.size();
    SlabPlan plan;
    plan.axis_path = casacore::IPosition::makeAxisPath(ndim);
    plan.slab = casacore::IPosition(ndim, 1);
    plan.slab(collapse_axis) = shape(collapse_axis);

    const std::uint64_t line_bytes = static_cast<std::uint64_t>(pixel_bytes) * static_cast<std::uint64_t>(shape(collapse_axis));
    const std::uint64_t lines = limit_bytes / std::max<std::uint64_t>(1, line_bytes);
    if (lines <= 1) {
        // Can only go line by line.
        return plan;
    }

    // Each axis takes no more than the room left, so the room never runs out: an axis past the budget
    // gets one.
    std::uint64_t room = lines;
    for (unsigned axis = 0; axis < ndim; ++axis) {
        if (axis != collapse_axis) {
            plan.slab(axis) = static_cast<ssize_t>(std::min<std::uint64_t>(room, static_cast<std::uint64_t>(shape(axis))));
            room /= static_cast<std::uint64_t>(plan.slab(axis));
        }
    }
    return plan;
}

} // namespace

double SlabPlan::StoreReads() const {
    if (!chunked) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return static_cast<double>(unit_bytes) / static_cast<double>(std::max<std::uint64_t>(1, budget_bytes));
}

// The collapser wants whole lines along the collapse axis, so a slab has to span that axis in full;
// the only freedom is how much display area it covers. A chunked store hands back a whole chunk
// however few of its pixels were asked for, so the unit that costs nothing to read twice is one chunk
// of display area by the full depth --
//
//     chunk_x * chunk_y * depth * bytes per pixel
//
// -- and a slab narrower than that is re-decoded by the ratio it falls short. That ratio is the whole
// story of what stepping through the image costs. The byte budget's 20 MB covers a 514 x 1 sliver of
// a 7776-deep cube chunked 512x512x4, which reads every chunk about a thousand times.
//
// Measured on an ASKAP cube chunked the same way, 7763 x 4742, one AVERAGE moment, against the 1 GiB
// decoded-chunk cache the server ran with: at 16, 32 and 64 channels the byte-budget shape read the
// store 1.0x over -- its working set still fit the cache -- and at 128 it read it 57x over and went
// from 17.7 s to 127.7 s. The cliff is where (chunks a slab spans) x (chunks along the collapse axis)
// stops fitting in the cache, and a deep cube is always past it.
//
// So: give the fastest display axis exactly one chunk, spend what is left on the next, and step the
// axes that were cut short before the ones that were not, so the reading finishes a chunk before it
// moves off it. Then the cache is no longer load-bearing, which is why its size is not asked for here:
// the measurements above are what it cost to lean on it, not a setting to plan against.
SlabPlan PlanSlab(const casacore::IPosition& shape, const casacore::IPosition& decode_unit, unsigned collapse_axis, unsigned pixel_bytes,
    std::uint64_t memory_bytes) {
    const unsigned ndim = shape.size();
    if (decode_unit.size() != ndim || collapse_axis >= ndim || shape(collapse_axis) < 1) {
        return PlanByBytes(shape, collapse_axis, pixel_bytes);
    }

    // Only for an image that really is granular -- a unit that covers the whole image is telling us
    // there is nothing to align to. The collapse axis counts: a cube can be exactly one chunk wide in
    // both spatial axes and still be thousands of chunks deep, and that is the shape this is worst on,
    // not the shape it can ignore.
    bool granular = false;
    for (unsigned axis = 0; axis < ndim; ++axis) {
        if (decode_unit(axis) < 1) {
            return PlanByBytes(shape, collapse_axis, pixel_bytes);
        }
        if (decode_unit(axis) < shape(axis)) {
            granular = true;
        }
    }
    if (!granular) {
        return PlanByBytes(shape, collapse_axis, pixel_bytes);
    }

    const std::uint64_t line_bytes = static_cast<std::uint64_t>(pixel_bytes) * static_cast<std::uint64_t>(shape(collapse_axis));

    // What the whole unit would cost, and what this process will spend on it. A moment is a
    // deliberate, one-at-a-time operation, so it may hold more than an interactive read -- but it
    // shares the server, hence the fraction and the ceiling.
    //
    // The ceiling is not a compromise between memory and speed; measured, it is the fastest point.
    // Sweeping it on 512 x 512 x 7776 chunked 512x512x4, warm, everything else fixed:
    //
    //     256 MiB  reads the store 29.0x  70.1 s
    //     512 MiB                  14.6x  74.2 s
    //       1 GiB                   7.3x  45.6 s
    //       2 GiB                   3.7x  39.5 s
    //       4 GiB                   2.3x  40.2 s
    //     9.7 GiB (the whole unit)  1.6x  75-80 s
    //
    // The curve is a U. Below the knee the decode dominates, as expected. Above it the slab stops
    // fitting anything -- a profile along the collapse axis is strided by the whole display area, so
    // every line touches thousands of pages, and 2.4x fewer bytes read cost 2x the time. Reading less
    // is not the objective; it is only the proxy that holds on the way up to the knee.
    //
    // So the ceiling is the measured optimum and the fraction only matters on machines too small to
    // reach it. A sixteenth puts a 16 GB machine at 1 GiB rather than the 512 MiB a thirty-second
    // would, and 512 MiB measured worse than 256.
    //
    // The machine's total memory, not its free memory: free memory tracks MemFree, which a full page
    // cache drives to near nothing while MemAvailable stays whole -- 15.7 GB against 125.3 GB on the
    // machine this was measured on. Sizing off it makes the speed depend on how much unrelated file
    // data happens to be cached, and backwards at that, since cached pages are the reclaimable ones.
    // Measured cost of the mistake: the same moment picked a 640 MiB slab one run and a ~1.2 GiB one
    // the next, reading a 7776-deep cube 8x over instead of 4x.
    static const std::uint64_t most_bytes = std::uint64_t(2) << 30;
    static const std::uint64_t least_bytes = std::uint64_t(64) << 20;
    std::uint64_t unit_bytes = line_bytes;
    for (unsigned axis = 0; axis < ndim; ++axis) {
        if (axis != collapse_axis) {
            unit_bytes *= static_cast<std::uint64_t>(std::min(decode_unit(axis), shape(axis)));
        }
    }
    std::uint64_t budget = std::min<std::uint64_t>(memory_bytes / 16, most_bytes);
    budget = std::max(budget, least_bytes);
    budget = std::min(budget, unit_bytes);

    SlabPlan plan;
    plan.chunked = true;
    plan.unit_bytes = unit_bytes;
    plan.budget_bytes = budget;
    plan.slab = casacore::IPosition(ndim, 1);
    plan.slab(collapse_axis) = shape(collapse_axis);

    std::uint64_t room = std::max<std::uint64_t>(1, budget / std::max<std::uint64_t>(1, line_bytes));
    std::vector<unsigned> grown;
    for (unsigned axis = 0; axis < ndim; ++axis) {
        if (axis == collapse_axis) {
            continue;
        }
        const auto want = static_cast<std::uint64_t>(std::min(decode_unit(axis), shape(axis)));
        const std::uint64_t take = std::max<std::uint64_t>(1, std::min(want, room));
        plan.slab(axis) = static_cast<ssize_t>(take);
        room = std::max<std::uint64_t>(1, room / take);
        grown.push_back(axis);
    }

    // Reverse of the order they were grown in: the last one to be grown is the one that ran out of
    // budget, and stepping it first keeps the reading inside the chunk the earlier axes paid for.
    plan.axis_path = casacore::IPosition(ndim);
    unsigned at = 0;
    for (auto axis = grown.rbegin(); axis != grown.rend(); ++axis) {
        plan.axis_path(at++) = static_cast<ssize_t>(*axis);
    }
    plan.axis_path(at) = static_cast<ssize_t>(collapse_axis);
    return plan;
}

} // namespace carta
