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

// The mean, over the units an axis of `length` touches when its corner lies `origin` into their grid,
// of the slabs of `step` from that corner that touch each.
double TouchesPerUnit(ssize_t origin, ssize_t length, ssize_t unit, ssize_t step) {
    std::size_t units = 0;
    std::size_t touches = 0;
    for (ssize_t start = (origin / unit) * unit; start < origin + length; start += unit) {
        const ssize_t from = std::max(start, origin) - origin;
        const ssize_t to = std::min(start + unit, origin + length) - origin;
        touches += static_cast<std::size_t>((to - 1) / step - from / step + 1);
        ++units;
    }
    return static_cast<double>(touches) / static_cast<double>(std::max<std::size_t>(1, units));
}

// The most units of `unit` that one slab of `step`, stepped from an axis's corner lying `origin` into
// their grid, touches along an axis of `length`.
std::uint64_t MostUnitsPerSlab(ssize_t origin, ssize_t length, ssize_t unit, ssize_t step) {
    std::uint64_t most = 0;
    for (ssize_t from = 0; from < length; from += step) {
        const ssize_t to = std::min(from + step, length);
        most = std::max<std::uint64_t>(most, static_cast<std::uint64_t>((origin + to - 1) / unit - (origin + from) / unit + 1));
    }
    return most;
}

// The least a moment spends on a slab, or on a cache, whatever the machine; also what a machine that
// will not say how much memory it has is given.
const std::uint64_t kLeastBytes = std::uint64_t(64) << 20;

// The most a moment spends on a cache of its own, however large the machine. A sixteenth of 125 GB
// let one moment over the cigar of the standard test set (512 x 512 x 30000 chunked 128x128x64, 20
// slabs 128 x 111) hold 3.7 GiB, which took it to 8.5 GB at its peak where the FITS cube's moment
// peaked at 5.1 GB, for a walk that decoded the store once rather than twice. Swept, warm, everything
// else fixed, two runs each:
//
//     cache       time     peak
//     3.7 GiB    41.0 s   8.5 GB
//       2 GiB    41.4 s   5.7 GB
//       1 GiB    42.5 s   4.4 GB
//     512 MiB    43.1 s   3.6 GB
//           0    43.5 s   3.0 GB
//
// Decoding from a warm page cache is a quarter of the walk and the collapse the rest, so a second
// decode costs little and the gigabytes past the first buy almost none of it back: 1 GiB is the knee.
// It is also what the server's own chunk cache holds by default.
const std::uint64_t kMostCacheBytes = std::uint64_t(1) << 30;

// Whole lines along the collapse axis, as many as a fixed budget holds, filling the other axes in
// their natural order. For an image with no chunking to align to.
SlabPlan PlanByBytes(const casacore::IPosition& shape, unsigned collapse_axis, unsigned pixel_bytes) {
    // Arbitrary, but reasonable.
    static const std::uint64_t limit_bytes = 20000000;

    const unsigned ndim = shape.size();
    SlabPlan plan;
    plan.store_reads = std::numeric_limits<double>::quiet_NaN();
    plan.cached_store_reads = std::numeric_limits<double>::quiet_NaN();
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

// The collapser wants whole lines along the collapse axis, so a slab has to span that axis in full;
// the only freedom is how much display area it covers. A chunked store hands back a whole chunk
// however few of its pixels were asked for, so the unit that costs nothing to read twice is one chunk
// of display area by the full depth --
//
//     chunk_x * chunk_y * depth * bytes per pixel
//
// -- and each chunk is decoded once for every slab that touches it. That count is the whole story of
// what stepping through the image costs. The byte budget's 20 MB covers a 514 x 1 sliver of
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
// moves off it.
//
// Measured on 2048 x 2048 x 2000 chunked 512x512x1, cut from the same ASKAP cube, one AVERAGE moment,
// 1 GiB cache, 2 GiB budget, from disk and from the page cache: a slab shaped to the chunk rather than
// to an advice four chunks wide took 45.5 s to 35.2 s from disk and 34.2 s to 23.2 s warm, decoding
// the store 1.7 times over rather than 5.4. Two refinements that decode less were slower still, and
// are not made: splitting a chunk the budget cannot hold into even pieces (256 of 512 rows rather than
// the 419 the budget allows; the collapse slowed by a third), and cutting slabs on the chunk grid for
// a region whose corner is off it (read slice by slice, each slab cost a quarter of a second more). A
// cache large enough to keep every chunk a slab touches until the next slab has read it (4 GiB there)
// took the decodes to one each and the warm time to 19.9 s. That was the server's shared cache; the
// plan does not count on the server's setting, and asks for a cache of the moment's own instead (see
// SlabPlan::CacheBytes).
SlabPlan PlanSlab(const casacore::IPosition& shape, const casacore::IPosition& decode_unit, unsigned collapse_axis, unsigned pixel_bytes,
    std::uint64_t memory_bytes, const casacore::IPosition& grid_origin) {
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
    // Sweeping it on 512 x 512 x 7776 chunked 512x512x4, warm, everything else fixed, with the reads
    // as this plan once estimated them, the unit over the budget:
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
    // is not the objective; it is only the proxy that holds on the way up to the knee. The same held
    // on the 2048 x 2048 x 2000 cube above: stretching the budget to 2.5 GiB so that a slab held one
    // whole chunk decoded each chunk once, and took 29.1 s against 23.2, all of it in the collapse.
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
    std::uint64_t unit_bytes = line_bytes;
    for (unsigned axis = 0; axis < ndim; ++axis) {
        if (axis != collapse_axis) {
            unit_bytes *= static_cast<std::uint64_t>(std::min(decode_unit(axis), shape(axis)));
        }
    }
    std::uint64_t budget = std::min<std::uint64_t>(memory_bytes / 16, most_bytes);
    budget = std::max(budget, kLeastBytes);
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

    // Where the grid lies is known only when the caller says; otherwise it is taken to start at the
    // image's corner, which a region cut from elsewhere may not.
    const bool on_the_grid = grid_origin.size() == ndim;
    // The axis stepped first among those with more than one slab: the one along which the slab stepped
    // next shares chunks with the last.
    int stepped_first = -1;
    for (unsigned at = 0; at < ndim && stepped_first < 0; ++at) {
        const auto axis = static_cast<unsigned>(plan.axis_path(at));
        if (plan.slab(axis) < shape(axis)) {
            stepped_first = static_cast<int>(axis);
        }
    }

    plan.store_reads = 1.0;
    plan.cached_store_reads = 1.0;
    std::uint64_t units_per_slab = 1;
    std::uint64_t unit_pixels = 1;
    for (unsigned axis = 0; axis < ndim; ++axis) {
        const ssize_t origin = on_the_grid ? grid_origin(axis) : 0;
        const double touches = TouchesPerUnit(origin, shape(axis), decode_unit(axis), plan.slab(axis));
        plan.store_reads *= touches;
        plan.cached_store_reads *= static_cast<int>(axis) == stepped_first ? 1.0 : touches;
        units_per_slab *= MostUnitsPerSlab(origin, shape(axis), decode_unit(axis), plan.slab(axis));
        unit_pixels *= static_cast<std::uint64_t>(decode_unit(axis));
    }
    // A chunk is decoded whole, edge or not, so it is kept whole.
    plan.reuse_chunks = plan.store_reads > 1.0 ? units_per_slab : 0;
    plan.reuse_pixels = plan.reuse_chunks * unit_pixels;
    plan.cache_ceiling_bytes = std::max(std::min(memory_bytes / 16, kMostCacheBytes), kLeastBytes);
    return plan;
}

std::uint64_t SlabPlan::CacheBytes(unsigned decoded_pixel_bytes) const {
    return std::min(reuse_pixels * decoded_pixel_bytes, cache_ceiling_bytes);
}

std::uint64_t SlabPlan::CacheBytesOfChunks(std::uint64_t decoded_chunk_bytes) const {
    return std::min(reuse_chunks * decoded_chunk_bytes, cache_ceiling_bytes);
}

} // namespace carta
