/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGESTATS_DERIVEDSTATISTICS_H_
#define CARTA_SRC_IMAGESTATS_DERIVEDSTATISTICS_H_

namespace carta {

// The count, sum, sum of squares, smallest and largest of the valid pixels of a plane, a cube or one
// channel of a region. See Totals in CONTEXT.md.
//
// Doubles all, the count too: a count is exact in one up to 2^53, and every caller either holds it
// as a double or converts it to one to divide by it.
struct Totals {
    double num_pixels = 0.0;
    double sum = 0.0;
    double sum_sq = 0.0;
    double min = 0.0;
    double max = 0.0;
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
// report the same bits. Sigma is `sqrt((sum_sq - sum * sum / n) / (n - 1))`, whose radicand can come
// out a rounding error below zero for pixels that are nearly all one value; it is then taken as zero,
// where it used to make sigma NaN. Rounding the other way still leaves such a sigma a little above
// zero: the totals hold no more than that, and a centred second moment is what would.
DerivedStatistics DeriveStatistics(const Totals& totals, LonePixelSigma lone_pixel_sigma);

} // namespace carta

#endif // CARTA_SRC_IMAGESTATS_DERIVEDSTATISTICS_H_
