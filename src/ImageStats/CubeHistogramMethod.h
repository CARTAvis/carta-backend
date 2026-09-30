/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGESTATS_CUBEHISTOGRAMMETHOD_H_
#define CARTA_SRC_IMAGESTATS_CUBEHISTOGRAMMETHOD_H_

#include <cstdint>
#include <string>

namespace carta {

// How a cube histogram should be computed.
//
// Exact is two passes over the pixels: one to find the range, one to bin over it. The other two are
// one pass, which halves the reading and gives up where the bin edges land -- see
// carta::zarr::CubeHistogramRequest. They are here to be measured, not to be defaults.
//
// Chosen by the session, which holds the setting, and applied by Frame::CalculateCubeHistogram,
// which takes one pass only when its loader has such a walk and the request leaves the bounds free.
// Only the Zarr loader has one, which is why the setting is still called zarr_histogram_method: it
// tells a user where it has an effect. A loader never reads it.
struct CubeHistogramMethod {
    bool one_pass = false;
    // Take every nth pixel along both spatial axes. One reads every pixel.
    std::uint64_t spatial_sample = 1;
};

// The zarr_histogram_method setting: "exact" (the default), "binned", or "sampled" with an optional
// stride, as in "sampled:4". What it does not recognise it logs and answers exact, so a typo is two
// passes rather than a silently sampled answer.
CubeHistogramMethod ParseCubeHistogramMethod(const std::string& method);

} // namespace carta

#endif // CARTA_SRC_IMAGESTATS_CUBEHISTOGRAMMETHOD_H_
