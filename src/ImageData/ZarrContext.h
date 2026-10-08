/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_ZARRCONTEXT_H_
#define CARTA_SRC_IMAGEDATA_ZARRCONTEXT_H_

#include <carta-zarr/carta_zarr.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace carta {

// Configure and eagerly create the process-wide carta-zarr context. This must be called once during
// backend startup, before any Zarr file is opened.
void ConfigureZarrContext(
    int file_io_concurrency, int data_copy_concurrency, int cache_pool_mb, int omp_thread_count);

// Returns the context configured at startup. A Context is already a shared handle, so this is a
// copy of the one handle rather than a pointer to it.
carta::zarr::Context GetZarrContext();

// The bytes the shared context's cache holds, as ConfigureZarrContext was told: 0 for one that holds
// nothing, and before it is called, when the context has TensorStore's default, which holds nothing too.
std::size_t ZarrCacheBytes();

}  // namespace carta

#endif  // CARTA_SRC_IMAGEDATA_ZARRCONTEXT_H_
