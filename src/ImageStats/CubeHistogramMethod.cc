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
    // "sampled", or "sampled:" and a stride. Any word beginning with it used to be taken for it, so
    // "sampled_typo" sampled every fourth pixel where a misspelling is meant to stay exact.
    if (method == "sampled" || method.rfind("sampled:", 0) == 0) {
        parsed.one_pass = true;
        parsed.spatial_sample = 4;
        const auto colon = method.find(':');
        if (colon != std::string::npos) {
            // Digits and nothing else: std::stoull skips space, takes a sign and stops at the first
            // character it cannot read, so "-1" was the largest stride there is and "8x" was 8.
            const auto digits = method.substr(colon + 1);
            bool read = !digits.empty() && digits.find_first_not_of("0123456789") == std::string::npos;
            if (read) {
                try {
                    const auto stride = std::stoull(digits);
                    if (stride > 0) {
                        parsed.spatial_sample = stride;
                    }
                } catch (const std::exception&) {
                    read = false;
                }
            }
            if (!read) {
                spdlog::warn("Ignoring the stride in zarr_histogram_method '{}'; using {}", method, parsed.spatial_sample);
            }
        }
        return parsed;
    }
    spdlog::warn("Unknown zarr_histogram_method '{}'; cube histograms stay exact", method);
    return parsed;
}

} // namespace carta
