/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGESTATS_DERIVEDSTATISTICS_H_
#define CARTA_SRC_IMAGESTATS_DERIVEDSTATISTICS_H_

#include <optional>

namespace carta {

// The count, sum, sum of squares, smallest and largest of the valid pixels of a plane, a cube or one
// channel of a region. See Totals in CONTEXT.md.
//
// Doubles all, the count too: a count is exact in one up to 2^53, and every caller either holds it
// as a double or converts it to one to divide by it.
//
// sum_sq_dev is the sum of the pixels' squared deviations from their own mean -- their Spread -- and
// is there only where whoever counted the pixels counted it, which is carta-zarr. See Spread in
// CONTEXT.md.
struct Totals {
    double num_pixels = 0.0;
    double sum = 0.0;
    double sum_sq = 0.0;
    double min = 0.0;
    double max = 0.0;
    std::optional<double> sum_sq_dev;
};

// Two sets of pixels' spreads put together, by the formula of Chan, Golub and LeVeque: what a cube's
// statistics joined from its planes' need. Each set is a few hundred thousand pixels or more, so
// their means, had from their sums, are good to far more digits than the term they go into needs.
struct Spread {
    double count = 0.0;
    double mean = 0.0;
    double sum_sq_dev = 0.0;

    void Merge(const Spread& other);
};

// What to report as the sigma of exactly one valid pixel, which has no sample standard deviation.
// It is the consumer's policy, and the callers differ on purpose: a spectral profile reports `zero`,
// and a plane or a cube reports `nan`. See Lone-pixel sigma in CONTEXT.md.
enum class LonePixelSigma { zero, nan };

// The statistics CARTA reports that are made from Totals. See Derived statistics in CONTEXT.md.
struct DerivedStatistics {
    double mean;
    double rms;
    double sigma;
    double extrema;
};

// Whichever of the smallest and the largest has the larger magnitude, the largest when they tie. It is
// the extrema of the Derived statistics, and here on its own for a caller that has the extremes and not
// the totals: region statistics, which casacore makes.
double Extrema(double min, double max);

// Derives the statistics from the totals, and from nothing else.
//
// With no valid pixel there is nothing to derive, and all four are NaN; a caller need not ask first,
// though it may have reasons of its own to. The extrema is Extrema(min, max).
//
// The arithmetic is the same however the totals arrived, so two loaders that count the same pixels
// report the same bits. Sigma is `sqrt(sum_sq_dev / (n - 1))` when the totals have the spread.
//
// When they do not -- every loader but the Zarr one -- it is `sqrt((sum_sq - sum * sum / n) / (n - 1))`,
// as it always was, and NaN when that radicand comes out below zero. That subtraction is of two
// numbers that agree in every digit a double holds once the pixels are far from zero against their
// spread, so it can also come out a little above zero, or far from right; ADR 0018 in carta-zarr
// says by how much, and is why the Zarr loader counts the spread instead.
DerivedStatistics DeriveStatistics(const Totals& totals, LonePixelSigma lone_pixel_sigma);

} // namespace carta

#endif // CARTA_SRC_IMAGESTATS_DERIVEDSTATISTICS_H_
