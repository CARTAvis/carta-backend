/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_ZARRCONTEXT_H_
#define CARTA_SRC_IMAGEDATA_ZARRCONTEXT_H_

#include <carta-zarr/carta_zarr.h>

namespace carta {

// The options of the process-wide carta-zarr context, from the zarr_* settings. A thread count of
// zero or less leaves file IO to carta-zarr's default and decoding to the OpenMP thread count; a
// cache size of zero or less is a cache that holds nothing.
carta::zarr::ContextOptions ZarrContextOptions(int file_io_concurrency, int data_copy_concurrency, int cache_pool_mb, int omp_thread_count);

// Creates the process-wide carta-zarr context. Called once at startup, before any Zarr file is
// opened, so that every Zarr image shares one set of thread pools and one chunk cache.
void ConfigureZarrContext(int file_io_concurrency, int data_copy_concurrency, int cache_pool_mb, int omp_thread_count);

// The context configured at startup, or one with carta-zarr's defaults if none was.
carta::zarr::Context GetZarrContext();

} // namespace carta

#endif // CARTA_SRC_IMAGEDATA_ZARRCONTEXT_H_
