/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGESTATS_BASICSTATSCALCULATOR_TCC_
#define CARTA_SRC_IMAGESTATS_BASICSTATSCALCULATOR_TCC_

#include "ImageStats/DerivedStatistics.h"
#include "Logger/Logger.h"
#include "Util/Nan.h"

#include <cmath>

namespace carta {

template <typename T>
void BasicStats<T>::join(const BasicStats<T>& other) {
    if (other.num_pixels) {
        sum += other.sum;
        sumSq += other.sumSq;
        num_pixels += other.num_pixels;
        min_val = std::min(min_val, other.min_val);
        max_val = std::max(max_val, other.max_val);
        // The extremes are not derived from here, but they are part of the totals.
        const auto derived = DeriveStatistics(
            {static_cast<double>(num_pixels), sum, sumSq, static_cast<double>(min_val), static_cast<double>(max_val)}, LonePixelSigma::nan);
        mean = derived.mean;
        stdDev = derived.sigma;
        rms = derived.rms;
    }
}

template <typename T>
BasicStats<T>::BasicStats(size_t num_pixels, double sum, double mean, double stdDev, T min_val, T max_val, double rms, double sumSq)
    : num_pixels(num_pixels), sum(sum), mean(mean), stdDev(stdDev), min_val(min_val), max_val(max_val), rms(rms), sumSq(sumSq) {}

// No pixel yet: nothing is derived, as for a plane with no valid pixel, and the extremes are the
// identities a join starts from.
template <typename T>
BasicStats<T>::BasicStats()
    : num_pixels(0),
      sum(0),
      mean(DOUBLE_NAN),
      stdDev(DOUBLE_NAN),
      min_val(std::numeric_limits<T>::max()),
      max_val(std::numeric_limits<T>::lowest()),
      rms(DOUBLE_NAN),
      sumSq(0) {}

template <typename T>
BasicStatsCalculator<T>::BasicStatsCalculator(const T* data, size_t data_size)
    : _min_val(std::numeric_limits<T>::max()),
      _max_val(std::numeric_limits<T>::lowest()),
      _sum(0),
      _sum_squares(0),
      _num_pixels(0),
      _data(data),
      _data_size(data_size) {}

template <typename T>
void BasicStatsCalculator<T>::reduce() {
    size_t i;
#pragma omp parallel for private(i) shared(_data) reduction(min: _min_val) reduction(max:_max_val) reduction(+:_num_pixels) reduction(+:_sum) reduction(+:_sum_squares)
    for (i = 0; i < _data_size; i++) {
        T val = _data[i];
        if (std::isfinite(val)) {
            if (val < _min_val) {
                _min_val = val;
            }
            if (val > _max_val) {
                _max_val = val;
            }
            _num_pixels++;
            _sum += (double)val;
            _sum_squares += std::pow(val, 2);
        }
    }
}

template <typename T>
void BasicStatsCalculator<T>::join(BasicStatsCalculator<T>& other) { // NOLINT
    _min_val = std::min(_min_val, other._min_val);
    _max_val = std::max(_max_val, other._max_val);
    _num_pixels += other._num_pixels;
    _sum += other._sum;
    _sum_squares += other._sum_squares;
}

template <typename T>
BasicStats<T> BasicStatsCalculator<T>::GetStats() const {
    const auto derived = DeriveStatistics(
        {static_cast<double>(_num_pixels), _sum, _sum_squares, static_cast<double>(_min_val), static_cast<double>(_max_val)},
        LonePixelSigma::nan);
    return BasicStats<T>{_num_pixels, _sum, derived.mean, derived.sigma, _min_val, _max_val, derived.rms, _sum_squares};
}

} // namespace carta

#endif // CARTA_SRC_IMAGESTATS_BASICSTATSCALCULATOR_TCC_
