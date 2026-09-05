/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ZarrContext.h"

#include <algorithm>
#include <cstddef>
#include <mutex>
#include <stdexcept>

#include <spdlog/spdlog.h>

namespace carta {
namespace {

constexpr std::size_t BYTES_PER_MB = 1024U * 1024U;

std::mutex& ContextMutex() {
    static std::mutex mutex;
    return mutex;
}

std::shared_ptr<const carta::zarr::Context>& SharedContext() {
    static std::shared_ptr<const carta::zarr::Context> context;
    return context;
}

std::shared_ptr<const carta::zarr::Context> CreateContext(const carta::zarr::OpenOptions& options) {
    auto context_result = carta::zarr::Context::Create(options);
    if (context_result) {
        return std::make_shared<const carta::zarr::Context>(std::move(context_result.value()));
    }

    spdlog::warn("Failed to create carta-zarr context from settings ({}); falling back to defaults",
                 context_result.error().message);
    auto default_context = carta::zarr::Context::Create();
    if (!default_context) {
        throw std::runtime_error("Failed to create default carta-zarr context: " + default_context.error().message);
    }
    return std::make_shared<const carta::zarr::Context>(std::move(default_context.value()));
}

}  // namespace

void ConfigureZarrContext(int file_io_concurrency, int data_copy_concurrency, int cache_pool_mb, int omp_thread_count) {
    carta::zarr::OpenOptions options;
    options.io_threads = file_io_concurrency > 0 ? static_cast<unsigned int>(file_io_concurrency) : 0;

    const int effective_data_copy_threads =
        data_copy_concurrency > 0 ? data_copy_concurrency : std::max(1, omp_thread_count - file_io_concurrency);
    options.decode_threads = static_cast<unsigned int>(effective_data_copy_threads);

    if (cache_pool_mb > 0) {
        options.cache_bytes = static_cast<std::size_t>(cache_pool_mb) * BYTES_PER_MB;
    } else {
        options.disable_cache = true;
    }

    auto context = CreateContext(options);
    std::scoped_lock lock(ContextMutex());
    SharedContext() = std::move(context);

    spdlog::debug("carta-zarr context: file_io_threads={}, data_copy_threads={}, cache_size_mb={}",
                  file_io_concurrency, effective_data_copy_threads, std::max(0, cache_pool_mb));
}

std::shared_ptr<const carta::zarr::Context> GetZarrContext() {
    std::scoped_lock lock(ContextMutex());
    if (!SharedContext()) {
        SharedContext() = CreateContext({});
    }
    return SharedContext();
}

}  // namespace carta
