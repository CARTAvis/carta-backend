/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "DerivedStatistics.h"

#include <cmath>
#include <limits>

namespace carta {

DerivedStatistics DeriveStatistics(const Totals& totals, LonePixelSigma lone_pixel_sigma) {
    constexpr double undefined = std::numeric_limits<double>::quiet_NaN();

    // Written so that a count that is not a count at all is nothing to derive from either.
    if (!(totals.num_pixels > 0.0)) {
        return {undefined, undefined, undefined, undefined};
    }

    const double count = totals.num_pixels;
    DerivedStatistics derived;
    derived.mean = totals.sum / count;
    derived.rms = std::sqrt(totals.sum_sq / count);
    if (count > 1.0) {
        derived.sigma = std::sqrt((totals.sum_sq - (totals.sum * totals.sum / count)) / (count - 1.0));
    } else {
        derived.sigma = lone_pixel_sigma == LonePixelSigma::zero ? 0.0 : undefined;
    }
    derived.extrema = std::abs(totals.min) > std::abs(totals.max) ? totals.min : totals.max;
    return derived;
}

} // namespace carta
