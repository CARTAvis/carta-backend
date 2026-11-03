/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_ZARRCONTEXT_H_
#define CARTA_SRC_IMAGEDATA_ZARRCONTEXT_H_

#include <carta-zarr/carta_zarr.h>

namespace carta {

// Returns the options of the process-wide carta-zarr context, made from the zarr_* settings.
// If the file IO thread count is zero or less, file IO uses min(4, the OpenMP thread count) threads.
// If the decode thread count is zero or less, decoding uses the OpenMP thread count.
// If the cache is zero GiB or less, there is no cache. The cache size may be a fraction of a GiB, such as 2.5.
carta::zarr::ContextOptions ZarrContextOptions(
    int file_io_concurrency, int data_copy_concurrency, double cache_pool_gib, int omp_thread_count);

carta::zarr::ContextOptions DefaultZarrContextOptions();

// Creates the process-wide carta-zarr context. Called once at startup, before any Zarr file is
// opened, so that every Zarr image shares one set of thread pools and one chunk cache.
void ConfigureZarrContext(int file_io_concurrency, int data_copy_concurrency, double cache_pool_gib, int omp_thread_count);

// Returns the context that ConfigureZarrContext created at startup.
// If ConfigureZarrContext was never called, it returns a context with the backend's default options.
carta::zarr::Context GetZarrContext();

} // namespace carta

#endif // CARTA_SRC_IMAGEDATA_ZARRCONTEXT_H_
