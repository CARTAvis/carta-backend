/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_ZARRCONTEXT_H_
#define CARTA_SRC_IMAGEDATA_ZARRCONTEXT_H_

#include <carta-zarr/carta_zarr.h>

#include <cstdint>
#include <memory>
#include <string>

namespace carta {

// Configure and eagerly create the process-wide carta-zarr context. This must be called once during
// backend startup, before any Zarr file is opened.
void ConfigureZarrContext(
    int file_io_concurrency, int data_copy_concurrency, int cache_pool_mb, int omp_thread_count);

// Returns the context configured at startup.
std::shared_ptr<const carta::zarr::Context> GetZarrContext();

// How a cube histogram over a Zarr store should be computed.
//
// Exact is two passes over the pixels: one to find the range, one to bin over it. The other two are
// one pass, which halves the reading and gives up where the bin edges land -- see
// carta::zarr::CubeHistogramRequest. They are here to be measured, not to be defaults.
struct ZarrHistogramSettings {
    bool one_pass = false;
    // Take every nth pixel along both spatial axes. One reads every pixel.
    std::uint64_t spatial_sample = 1;
};

// Parse the zarr_histogram_method setting: "exact" (the default), "binned", or "sampled" with an
// optional stride, as in "sampled:4". An unrecognised value logs and leaves the default.
void ConfigureZarrHistogram(const std::string& method);

ZarrHistogramSettings GetZarrHistogramSettings();

}  // namespace carta

#endif  // CARTA_SRC_IMAGEDATA_ZARRCONTEXT_H_
