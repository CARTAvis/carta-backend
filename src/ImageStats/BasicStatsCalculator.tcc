/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGESTATS_BASICSTATSCALCULATOR_TCC_
#define CARTA_BACKEND_IMAGESTATS_BASICSTATSCALCULATOR_TCC_

#include "Logger/Logger.h"

#include <cmath>

namespace carta {

template <typename T>
void BasicStats<T>::join(BasicStats<T>& other) {
    if (other.num_pixels) {
        sum += other.sum;
        sumSq += other.sumSq;
        num_pixels += other.num_pixels;
        min_val = std::min(min_val, other.min_val);
        max_val = std::max(max_val, other.max_val);
        mean = sum / num_pixels;
        stdDev = num_pixels > 1 ? sqrt((sumSq - (sum * sum / num_pixels)) / (num_pixels - 1)) : NAN;
        rms = sqrt(sumSq / num_pixels);
    }
}

template <typename T>
BasicStats<T>::BasicStats(size_t num_pixels, double sum, double mean, double stdDev, T min_val, T max_val, double rms, double sumSq)
    : num_pixels(num_pixels), sum(sum), mean(mean), stdDev(stdDev), min_val(min_val), max_val(max_val), rms(rms), sumSq(sumSq) {}

template <typename T>
BasicStats<T>::BasicStats()
    : num_pixels(0),
      sum(0),
      mean(0),
      stdDev(0),
      min_val(std::numeric_limits<T>::max()),
      max_val(std::numeric_limits<T>::lowest()),
      rms(0),
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
            _sum_squares += (double)val * (double)val;
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
    double mean;
    double stdDev;
    double rms;

    if (_num_pixels > 0) {
        mean = _sum / _num_pixels;
        stdDev = _num_pixels > 1 ? sqrt((_sum_squares - (_sum * _sum / _num_pixels)) / (_num_pixels - 1)) : NAN;
        rms = sqrt(_sum_squares / _num_pixels);
    } else {
        mean = NAN;
        stdDev = NAN;
        rms = NAN;
    }

    return BasicStats<T>{_num_pixels, _sum, mean, stdDev, _min_val, _max_val, rms, _sum_squares};
}

} // namespace carta

#endif // CARTA_BACKEND_IMAGESTATS_BASICSTATSCALCULATOR_TCC_
