/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_ZARRCONTEXT_H_
#define CARTA_SRC_IMAGEDATA_ZARRCONTEXT_H_

#include <cstddef>

#include <carta-zarr/carta_zarr.h>

namespace carta {

// The options of the process-wide carta-zarr context, from the zarr_* settings. A thread count of
// zero or less sets file IO to min(4, the OpenMP thread count) and decoding to the OpenMP thread count; a
// cache of zero GiB or less holds nothing. The cache size may be a fraction of a GiB, such as 2.5.
carta::zarr::ContextOptions ZarrContextOptions(
    int file_io_concurrency, int data_copy_concurrency, double cache_pool_gib, int omp_thread_count);

// Creates the process-wide carta-zarr context. Called once at startup, before any Zarr file is
// opened, so that every Zarr image shares one set of thread pools and one chunk cache.
void ConfigureZarrContext(int file_io_concurrency, int data_copy_concurrency, double cache_pool_gib, int omp_thread_count);

// The context configured at startup, or one with carta-zarr's defaults if none was.
carta::zarr::Context GetZarrContext();

// The bytes of decoded chunks the shared cache may hold, as configured at startup; zero when none was.
std::size_t ZarrCacheBytes();

} // namespace carta

#endif // CARTA_SRC_IMAGEDATA_ZARRCONTEXT_H_
