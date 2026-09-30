/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "CubeHistogramMethod.h"

#include <exception>

#include <spdlog/spdlog.h>

namespace carta {

CubeHistogramMethod ParseCubeHistogramMethod(const std::string& method) {
    CubeHistogramMethod parsed;
    if (method.empty() || method == "exact") {
        return parsed;
    }
    if (method == "binned") {
        parsed.one_pass = true;
        return parsed;
    }
    if (method.rfind("sampled", 0) == 0) {
        parsed.one_pass = true;
        parsed.spatial_sample = 4;
        const auto colon = method.find(':');
        if (colon != std::string::npos) {
            try {
                const auto stride = std::stoull(method.substr(colon + 1));
                if (stride > 0) {
                    parsed.spatial_sample = stride;
                }
            } catch (const std::exception&) {
                spdlog::warn("Ignoring the stride in zarr_histogram_method '{}'; using {}", method, parsed.spatial_sample);
            }
        }
        return parsed;
    }
    spdlog::warn("Unknown zarr_histogram_method '{}'; cube histograms stay exact", method);
    return parsed;
}

} // namespace carta
