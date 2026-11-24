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

// Returns the mean number of slabs that touch each unit along one axis. `origin` is where the axis starts
// in the grid of units, and `step` is the slab length.
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

// Returns the most units that one slab touches along one axis. The arguments are as for TouchesPerUnit.
std::uint64_t MostUnitsPerSlab(ssize_t origin, ssize_t length, ssize_t unit, ssize_t step) {
    std::uint64_t most = 0;
    for (ssize_t from = 0; from < length; from += step) {
        const ssize_t to = std::min(from + step, length);
        most = std::max<std::uint64_t>(most, static_cast<std::uint64_t>((origin + to - 1) / unit - (origin + from) / unit + 1));
    }
    return most;
}

// The floor of the slab budget and of the cache ceiling. A machine that does not report its memory gets this.
const std::uint64_t kLeastBytes = std::uint64_t(64) << 20;

// The most that a moment spends on its own cache, however large the machine. Past 1 GiB, the decodes it saves
// are worth less than the memory it takes. 1 GiB is also the default size of the server's chunk cache.
const std::uint64_t kMostCacheBytes = std::uint64_t(1) << 30;

// Returns a slab of whole lines along the collapse axis, as many as a fixed budget holds. The other axes
// are filled in their natural order. For an image with no chunking to align to.
SlabPlan PlanByBytes(const casacore::IPosition& shape, unsigned collapse_axis, unsigned pixel_bytes, std::uint64_t result_bytes) {
    // Arbitrary, but reasonable.
    static const std::uint64_t limit_bytes = 20000000;

    const unsigned ndim = shape.size();
    SlabPlan plan;
    plan.store_reads = std::numeric_limits<double>::quiet_NaN();
    plan.cached_store_reads = std::numeric_limits<double>::quiet_NaN();
    plan.axis_path = casacore::IPosition::makeAxisPath(ndim);
    plan.slab = casacore::IPosition(ndim, 1);
    plan.slab(collapse_axis) = shape(collapse_axis);

    const std::uint64_t line_bytes =
        static_cast<std::uint64_t>(pixel_bytes) * static_cast<std::uint64_t>(shape(collapse_axis)) + result_bytes;
    const std::uint64_t lines = limit_bytes / std::max<std::uint64_t>(1, line_bytes);
    if (lines <= 1) {
        // Can only go line by line.
        return plan;
    }

    // Each axis takes no more than the room left, so the room never reaches 0. An axis past the budget gets 1.
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

// A slab spans the whole collapse axis, and each chunk is decoded once for every slab that touches it.
// So the fastest display axis gets one chunk, and the next axis gets what the budget leaves. The axes that
// were cut short are stepped first, so that the reading finishes a chunk before it moves off it.
SlabPlan PlanSlab(const casacore::IPosition& shape, const casacore::IPosition& decode_unit, unsigned collapse_axis, unsigned pixel_bytes,
    std::uint64_t memory_bytes, const casacore::IPosition& grid_origin, std::uint64_t result_bytes) {
    const unsigned ndim = shape.size();
    if (decode_unit.size() != ndim || collapse_axis >= ndim || shape(collapse_axis) < 1) {
        return PlanByBytes(shape, collapse_axis, pixel_bytes, result_bytes);
    }

    // Plan by chunks only if a unit is smaller than the image on some axis. The collapse axis counts too,
    // because a cube one chunk wide can still be thousands of chunks deep.
    bool granular = false;
    for (unsigned axis = 0; axis < ndim; ++axis) {
        if (decode_unit(axis) < 1) {
            return PlanByBytes(shape, collapse_axis, pixel_bytes, result_bytes);
        }
        if (decode_unit(axis) < shape(axis)) {
            granular = true;
        }
    }
    if (!granular) {
        return PlanByBytes(shape, collapse_axis, pixel_bytes, result_bytes);
    }

    const std::uint64_t line_bytes =
        static_cast<std::uint64_t>(pixel_bytes) * static_cast<std::uint64_t>(shape(collapse_axis)) + result_bytes;

    // The budget is a sixteenth of total memory, at most 2 GiB. A larger slab is slower, because the collapse slows more
    // than the decodes save. Use total memory, not free memory, because a full page cache makes free memory near 0.
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

    // Step the axes in the reverse order of growth. The last axis grown ran out of budget, and stepping it
    // first keeps the reading inside the chunk that the earlier axes paid for.
    plan.axis_path = casacore::IPosition(ndim);
    unsigned at = 0;
    for (auto axis = grown.rbegin(); axis != grown.rend(); ++axis) {
        plan.axis_path(at++) = static_cast<ssize_t>(*axis);
    }
    plan.axis_path(at) = static_cast<ssize_t>(collapse_axis);

    // The grid is known only when the caller gives it. Otherwise it is taken to start at the image's corner.
    const bool on_the_grid = grid_origin.size() == ndim;
    // The first axis in the path with more than one slab. Along it, the next slab shares chunks with the last one.
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
    // A chunk is decoded whole, even at an edge, so the cache keeps it whole.
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
