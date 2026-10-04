/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "DerivedStatistics.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace carta {

double Extrema(double min, double max) {
    return std::abs(min) > std::abs(max) ? min : max;
}

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
    if (count > 1.0 && totals.sum_sq_dev) {
        derived.sigma = std::sqrt(std::max(*totals.sum_sq_dev, 0.0) / (count - 1.0));
    } else if (count > 1.0) {
        derived.sigma = std::sqrt((totals.sum_sq - (totals.sum * totals.sum / count)) / (count - 1.0));
    } else {
        derived.sigma = lone_pixel_sigma == LonePixelSigma::zero ? 0.0 : undefined;
    }
    derived.extrema = Extrema(totals.min, totals.max);
    return derived;
}

void Spread::Merge(const Spread& other) {
    if (!(other.count > 0.0)) {
        return;
    }
    if (!(count > 0.0)) {
        *this = other;
        return;
    }
    const double total = count + other.count;
    const double between = other.mean - mean;
    sum_sq_dev += other.sum_sq_dev + (between * between * (count * other.count / total));
    mean += between * (other.count / total);
    count = total;
}

} // namespace carta
