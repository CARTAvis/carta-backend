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
#include <omp.h>
#include <spdlog/spdlog.h>

#include "Main/ProgramSettings.h"

namespace carta {
namespace {

constexpr std::size_t BYTES_PER_GIB = 1024U * 1024U * 1024U;

std::mutex context_mutex;
std::optional<carta::zarr::Context> shared_context;
// The cache size of the shared context
std::size_t shared_cache_bytes = 0;

// The options in use, each a number: the backend settles every one rather than leaving any to carta-zarr
void LogZarrContextOptions(const carta::zarr::ContextOptions& options) {
    spdlog::debug("carta-zarr context: file_io_threads={}, data_copy_threads={}, cache_size_gib={:g}", options.io_threads,
        options.decode_threads, static_cast<double>(options.cache_bytes.value_or(0)) / BYTES_PER_GIB);
}

// The context with `options`, or with the backend's defaults if carta-zarr refuses them; `used` is set to the
// options of the one made
carta::zarr::Context CreateContext(const carta::zarr::ContextOptions& options, carta::zarr::ContextOptions& used) {
    auto context = carta::zarr::Context::Create(options);
    if (context) {
        LogZarrContextOptions(options);
        used = options;
        return *std::move(context);
    }

    spdlog::warn("Failed to apply requested carta-zarr resource limits ({}); using the defaults", context.error().message);
    const auto defaults = DefaultZarrContextOptions();
    auto default_context = carta::zarr::Context::Create(defaults);
    if (!default_context) {
        throw casacore::AipsError("Failed to create default carta-zarr context: " + default_context.error().message);
    }
    LogZarrContextOptions(defaults);
    used = defaults;
    return *std::move(default_context);
}

// The shared context, made with the backend's defaults if none was configured; context_mutex is held
const carta::zarr::Context& SharedContext() {
    if (!shared_context) {
        carta::zarr::ContextOptions used;
        shared_context = CreateContext(DefaultZarrContextOptions(), used);
        shared_cache_bytes = used.cache_bytes.value_or(0);
    }
    return *shared_context;
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

carta::zarr::ContextOptions DefaultZarrContextOptions() {
    return ZarrContextOptions(ZARR_FILE_IO_CONCURRENCY, ZARR_DATA_COPY_CONCURRENCY, ZARR_CACHE_POOL_GIB, omp_get_max_threads());
}

void ConfigureZarrContext(int file_io_concurrency, int data_copy_concurrency, double cache_pool_gib, int omp_thread_count) {
    const auto options = ZarrContextOptions(file_io_concurrency, data_copy_concurrency, cache_pool_gib, omp_thread_count);
    carta::zarr::ContextOptions used;
    auto context = CreateContext(options, used);
    std::scoped_lock lock(context_mutex);
    shared_context = std::move(context);
    shared_cache_bytes = used.cache_bytes.value_or(0);
}

std::size_t ZarrCacheBytes() {
    std::scoped_lock lock(context_mutex);
    SharedContext();
    return shared_cache_bytes;
}

carta::zarr::Context GetZarrContext() {
    std::scoped_lock lock(context_mutex);
    return SharedContext();
}

} // namespace carta
