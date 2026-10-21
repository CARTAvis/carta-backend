/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ZarrContext.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <mutex>
#include <optional>
#include <string>

#include <casacore/casa/Exceptions/Error.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

namespace carta {
namespace {

constexpr std::size_t BYTES_PER_GIB = 1024U * 1024U * 1024U;

std::mutex context_mutex;
std::optional<carta::zarr::Context> shared_context;
// The cache size of the context configured at startup
std::size_t configured_cache_bytes = 0;

// The context with `options`, or with carta-zarr's defaults if it refuses them, as `applied` says
carta::zarr::Context CreateContext(const carta::zarr::ContextOptions& options, bool* applied = nullptr) {
    auto context = carta::zarr::Context::Create(options);
    if (applied) {
        *applied = context.has_value();
    }
    if (context) {
        spdlog::debug("carta-zarr context: file_io_threads={}, data_copy_threads={}, cache_size_gib={}", options.io_threads,
            options.decode_threads,
            options.cache_bytes ? fmt::format("{:g}", static_cast<double>(*options.cache_bytes) / BYTES_PER_GIB) : "default");
        return *std::move(context);
    }

    spdlog::warn("Failed to apply requested carta-zarr resource limits ({}); using carta-zarr defaults", context.error().message);
    auto default_context = carta::zarr::Context::Create();
    if (!default_context) {
        throw casacore::AipsError("Failed to create default carta-zarr context: " + default_context.error().message);
    }
    return *std::move(default_context);
}

} // namespace

carta::zarr::ContextOptions ZarrContextOptions(
    int file_io_concurrency, int data_copy_concurrency, double cache_pool_gib, int omp_thread_count) {
    carta::zarr::ContextOptions options;
    options.io_threads =
        static_cast<unsigned int>(file_io_concurrency > 0 ? file_io_concurrency : std::min(4, std::max(1, omp_thread_count)));
    // File IO threads mostly wait on reads, so they do not come out of the decode thread budget.
    options.decode_threads = static_cast<unsigned int>(data_copy_concurrency > 0 ? data_copy_concurrency : std::max(1, omp_thread_count));
    // Rounded down to whole bytes, and no more than a size can hold; zero, less or not a number holds nothing
    const double cache_bytes = cache_pool_gib > 0 ? cache_pool_gib * static_cast<double>(BYTES_PER_GIB) : 0.0;
    options.cache_bytes = cache_bytes < static_cast<double>(std::numeric_limits<std::size_t>::max())
                              ? static_cast<std::size_t>(cache_bytes)
                              : std::numeric_limits<std::size_t>::max();
    return options;
}

void ConfigureZarrContext(int file_io_concurrency, int data_copy_concurrency, double cache_pool_gib, int omp_thread_count) {
    const auto options = ZarrContextOptions(file_io_concurrency, data_copy_concurrency, cache_pool_gib, omp_thread_count);
    bool applied(false);
    auto context = CreateContext(options, &applied);
    std::scoped_lock lock(context_mutex);
    shared_context = std::move(context);
    // carta-zarr's defaults leave TensorStore's cache pool, which holds nothing
    configured_cache_bytes = applied ? options.cache_bytes.value_or(0) : 0;
}

std::size_t ZarrCacheBytes() {
    std::scoped_lock lock(context_mutex);
    return configured_cache_bytes;
}

carta::zarr::Context GetZarrContext() {
    std::scoped_lock lock(context_mutex);
    if (!shared_context) {
        shared_context = CreateContext({});
    }
    return *shared_context;
}

} // namespace carta
