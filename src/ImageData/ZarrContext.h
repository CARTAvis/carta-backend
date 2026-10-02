/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_ZARRCONTEXT_H_
#define CARTA_SRC_IMAGEDATA_ZARRCONTEXT_H_

#include <carta-zarr/carta_zarr.h>

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

// How much the context's shared cache holds, in bytes: what ConfigureZarrContext was given, zero when it
// was told to keep nothing, and TensorStore's own default size when it was never configured or left
// the size alone.
std::size_t ZarrCacheBytes();

}  // namespace carta

#endif  // CARTA_SRC_IMAGEDATA_ZARRCONTEXT_H_
