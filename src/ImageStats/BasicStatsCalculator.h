/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGESTATS_BASICSTATSCALCULATOR_H_
#define CARTA_BACKEND_IMAGESTATS_BASICSTATSCALCULATOR_H_

#include <algorithm>

#include <tbb/blocked_range2d.h>
#include <tbb/blocked_range3d.h>

namespace carta {

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
    const std::vector<T>& _data;

public:
    BasicStatsCalculator(const std::vector<T>& data);
    BasicStatsCalculator(BasicStatsCalculator& mm, tbb::split);

    void operator()(const tbb::blocked_range<size_t>& r);
    void join(BasicStatsCalculator& other); // NOLINT
    void reduce(const size_t start, const size_t end);

    BasicStats<T> GetStats() const;
};

} // namespace carta

#include "BasicStatsCalculator.tcc"

#endif // CARTA_BACKEND_IMAGESTATS_BASICSTATSCALCULATOR_H_
