/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGESTATS_BASICSTATSCALCULATOR_H_
#define CARTA_BACKEND_IMAGESTATS_BASICSTATSCALCULATOR_H_

#include <algorithm>
#include <cmath>

#include <carta-protobuf/defs.pb.h>

namespace carta {

template <typename T>
struct Bounds {
    T min;
    T max;

    Bounds<T>() : min(0), max(0) {}

    Bounds<T>(T min_, T max_) : min(min_), max(max_) {}

    Bounds<T>(const CARTA::DoubleBounds& bounds) : min(bounds.min()), max(bounds.max()) {}

    bool Equal(T num1, T num2) const {
        return fabs(num1 - num2) <= std::numeric_limits<T>::epsilon();
    }

    // U is the type of statistics values. When statistics values are unavailable, they are assigned to extreme values of U type
    template <typename U>
    bool Invalid() const {
        return min == std::numeric_limits<U>::max() || max == std::numeric_limits<U>::lowest();
    }

    bool operator==(const Bounds<T>& rhs) const {
        return Equal(min, rhs.min) && Equal(max, rhs.max);
    }

    bool operator!=(const Bounds<T>& rhs) const {
        return !Equal(min, rhs.min) || !Equal(max, rhs.max);
    }
};

template <typename T>
struct BasicStats {
    size_t num_pixels;
    double sum;
    double mean;
    double stdDev;
    T min_val;
    T max_val;
    double rms;
    double sumSq;

    BasicStats<T>(size_t num_pixels, double sum, double mean, double stdDev, T min_val, T max_val, double rms, double sumSq);
    BasicStats<T>();
    void join(BasicStats<T>& other);
};

template <typename T>
class BasicStatsCalculator {
    T _min_val, _max_val;
    double _sum, _sum_squares;
    size_t _num_pixels;
    const T* _data;
    size_t _data_size;

public:
    BasicStatsCalculator(const T* data, size_t data_size);

    void join(BasicStatsCalculator& other); // NOLINT
    void reduce();

    BasicStats<T> GetStats() const;
};

} // namespace carta

#include "BasicStatsCalculator.tcc"

#endif // CARTA_BACKEND_IMAGESTATS_BASICSTATSCALCULATOR_H_
