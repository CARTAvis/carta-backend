/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ZarrContext.h"

#include <algorithm>
#include <cstddef>
#include <mutex>
#include <optional>

#include <casacore/casa/Exceptions/Error.h>
#include <spdlog/spdlog.h>

namespace carta {
namespace {

constexpr std::size_t BYTES_PER_MB = 1024U * 1024U;

std::mutex& ContextMutex() {
    static std::mutex mutex;
    return mutex;
}

// Empty until the first of ConfigureZarrContext and GetZarrContext, which is the only reason this is
// an optional: a carta-zarr Context always refers to one.
std::optional<carta::zarr::Context>& SharedContext() {
    static std::optional<carta::zarr::Context> context;
    return context;
}

std::size_t& CacheBytes() {
    static std::size_t bytes = 0;
    return bytes;
}

carta::zarr::Context CreateContext(const carta::zarr::ContextOptions& options) {
    auto context_result = carta::zarr::Context::Create(options);
    if (context_result) {
        return *std::move(context_result);
    }

    spdlog::warn("Failed to apply requested carta-zarr resource limits ({}); using carta-zarr defaults",
                 context_result.error().message);
    auto default_context = carta::zarr::Context::Create();
    if (!default_context) {
        throw casacore::AipsError("Failed to create default carta-zarr context: " + default_context.error().message);
    }
    return *std::move(default_context);
}

}  // namespace

void ConfigureZarrContext(int file_io_concurrency, int data_copy_concurrency, int cache_pool_mb, int omp_thread_count) {
    carta::zarr::ContextOptions options;
    options.io_threads = file_io_concurrency > 0 ? static_cast<unsigned int>(file_io_concurrency) : 0;

    // I/O threads are mostly blocked on reads and do not reduce the decode thread budget.
    const int effective_data_copy_threads =
        data_copy_concurrency > 0 ? data_copy_concurrency : std::max(1, omp_thread_count);
    options.decode_threads = static_cast<unsigned int>(effective_data_copy_threads);

    // Zero is a size carta-zarr takes literally: the pool that holds nothing, which is what this
    // used to say with a separate disable_cache beside the size.
    options.cache_bytes = cache_pool_mb > 0 ? static_cast<std::size_t>(cache_pool_mb) * BYTES_PER_MB : 0;

    auto context = CreateContext(options);
    std::scoped_lock lock(ContextMutex());
    SharedContext() = std::move(context);
    CacheBytes() = options.cache_bytes.value_or(0);

    const std::string file_io_threads = file_io_concurrency > 0 ? std::to_string(file_io_concurrency) : "default";
    const std::string cache_size_mib = cache_pool_mb > 0 ? std::to_string(cache_pool_mb) : "disabled";
    spdlog::debug("carta-zarr context: file_io_threads={}, data_copy_threads={}, cache_size_mib={}",
                  file_io_threads, effective_data_copy_threads, cache_size_mib);
}

carta::zarr::Context GetZarrContext() {
    std::scoped_lock lock(ContextMutex());
    if (!SharedContext()) {
        SharedContext() = CreateContext({});
    }
    return *SharedContext();
}

std::size_t ZarrCacheBytes() {
    std::scoped_lock lock(ContextMutex());
    return CacheBytes();
}

}  // namespace carta
