/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_ZARR_ZARRCONTEXT_H_
#define CARTA_SRC_IMAGEDATA_ZARR_ZARRCONTEXT_H_

#include <cstddef>
#include <memory>

#include <carta-zarr/carta_zarr.h>

namespace carta {

// Returns the options of the process-wide carta-zarr context, made from the zarr_* settings.
// If the file IO thread count is zero or less, file IO uses min(4, the OpenMP thread count) threads.
// If the decode thread count is zero or less, decoding uses the OpenMP thread count.
// If the cache is zero GiB or less, there is no cache. The cache size may be a fraction of a GiB, such as 2.5.
carta::zarr::ContextOptions ZarrContextOptions(
    int file_io_concurrency, int data_copy_concurrency, double cache_pool_gib, int omp_thread_count);

carta::zarr::ContextOptions DefaultZarrContextOptions();

// Sets the options of the context that Zarr images share. Called once at startup, before any Zarr file is
// opened. If carta-zarr refuses the options, the backend's defaults are used.
void ConfigureZarrContext(int file_io_concurrency, int data_copy_concurrency, double cache_pool_gib, int omp_thread_count);

// Returns the context that every open Zarr image shares, with its thread pools and its chunk cache.
// An image holds the pointer while it is open. The backend itself keeps only a weak pointer, so the context
// and its cache are freed when the last image closes. The next call then makes a new one.
std::shared_ptr<const carta::zarr::Context> GetZarrContext();

// Returns the context that the file list uses to check stores and read their metadata. It has no cache and
// no decode threads of its own, so it can stay for the life of the process.
const carta::zarr::Context& GetZarrProbeContext();

// The size in bytes of the shared cache. It is the backend's default if ConfigureZarrContext
// was not called, or if its settings were refused.
std::size_t ZarrCacheBytes();

} // namespace carta

#endif // CARTA_SRC_IMAGEDATA_ZARR_ZARRCONTEXT_H_
