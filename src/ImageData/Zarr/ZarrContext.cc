/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ZarrContext.h"

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <limits>
#include <memory>
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
// Signalled when a shared context has been destroyed
std::condition_variable context_destroyed;
// The shared contexts made and not yet destroyed
int contexts_alive = 0;
// The options that ConfigureZarrContext set
std::optional<carta::zarr::ContextOptions> configured_options;
std::weak_ptr<const carta::zarr::Context> shared_context;

// The options in use
void LogZarrContextOptions(const carta::zarr::ContextOptions& options) {
    spdlog::debug("carta-zarr context: file_io_threads={}, data_copy_threads={}, cache_size_gib={:g}", options.io_threads,
        options.decode_threads, static_cast<double>(options.cache_bytes.value_or(0)) / BYTES_PER_GIB);
}

// Makes a context with `options`, or with the defaults if carta-zarr refuses them. Sets `used` to the options used.
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

// A shared context, with a count of the contexts not yet destroyed. The count goes down only after the
// context and its cache are gone, because members are destroyed in reverse order.
struct SharedContext {
    struct Count {
        ~Count() {
            {
                std::scoped_lock lock(context_mutex);
                --contexts_alive;
            }
            context_destroyed.notify_all();
        }
    } count;
    carta::zarr::Context context;

    explicit SharedContext(carta::zarr::Context made) : context(std::move(made)) {}
};

// Makes the shared context with `options`. The caller holds context_mutex.
std::shared_ptr<const carta::zarr::Context> MakeSharedContext(
    const carta::zarr::ContextOptions& options, carta::zarr::ContextOptions& used) {
    auto holder = std::make_shared<SharedContext>(CreateContext(options, used));
    ++contexts_alive;
    return {holder, &holder->context};
}

} // namespace

carta::zarr::ContextOptions ZarrContextOptions(
    int file_io_concurrency, int data_copy_concurrency, double cache_pool_gib, int omp_thread_count) {
    carta::zarr::ContextOptions options;
    options.io_threads =
        static_cast<unsigned int>(file_io_concurrency > 0 ? file_io_concurrency : std::min(4, std::max(1, omp_thread_count)));
    // File IO threads mostly wait on reads, so they do not come out of the decode thread budget.
    options.decode_threads = static_cast<unsigned int>(data_copy_concurrency > 0 ? data_copy_concurrency : std::max(1, omp_thread_count));
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
    // Made once here only to report refused options at startup
    carta::zarr::ContextOptions used;
    CreateContext(options, used);
    std::scoped_lock lock(context_mutex);
    configured_options = used;
}

std::shared_ptr<const carta::zarr::Context> GetZarrContext() {
    std::unique_lock lock(context_mutex);
    // Waits while the last context is still being destroyed, so that there is never more than one cache
    while (contexts_alive > 0) {
        if (auto context = shared_context.lock()) {
            return context;
        }
        context_destroyed.wait(lock);
    }
    carta::zarr::ContextOptions used;
    auto context = MakeSharedContext(configured_options.value_or(DefaultZarrContextOptions()), used);
    // A context that falls back to the defaults does not change the settings for the next one
    if (!configured_options) {
        configured_options = used;
    }
    shared_context = context;
    return context;
}

const carta::zarr::Context& GetZarrProbeContext() {
    // Never destroyed, because other threads may still use it while the process exits
    static const auto* context = [] {
        carta::zarr::ContextOptions options;
        {
            // The same file IO limit as the shared context, so that one slow listing does not hold up the others
            std::scoped_lock lock(context_mutex);
            options.io_threads = configured_options.value_or(DefaultZarrContextOptions()).io_threads;
        }
        options.decode_threads = 1;
        options.cache_bytes = 0;
        auto created = carta::zarr::Context::Create(options);
        if (!created) {
            throw casacore::AipsError("Failed to create the carta-zarr context for the file list: " + created.error().message);
        }
        return new carta::zarr::Context(*std::move(created));
    }();
    return *context;
}

std::size_t ZarrCacheBytes() {
    std::scoped_lock lock(context_mutex);
    return configured_options.value_or(DefaultZarrContextOptions()).cache_bytes.value_or(0);
}

} // namespace carta
