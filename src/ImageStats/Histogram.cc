/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Histogram.h"

#include <algorithm>
#include <cmath>

#include <omp.h>

#include "Threading.h"

using namespace carta;

Histogram::Histogram(int num_bins, float min_value, float max_value, const std::vector<float>& data)
    : _num_bins(num_bins), _bin_width((max_value - min_value) / num_bins), _min_val(min_value), _hist(num_bins, 0), _data(data) {}

Histogram::Histogram(Histogram& h, tbb::split)
    : _num_bins(h._num_bins), _bin_width(h._bin_width), _min_val(h._min_val), _hist(h._hist.size(), 0), _data(h._data) {}

void Histogram::operator()(const tbb::blocked_range<size_t>& r) {
    std::vector<int> tmp(_hist);
    for (auto i = r.begin(); i != r.end(); ++i) {
        auto v = _data[i];
        if (std::isnan(v) || std::isinf(v)) {
            continue;
        }

        size_t bin_number = std::max<size_t>(std::min<size_t>((size_t)((v - _min_val) / _bin_width), (size_t)_hist.size() - 1), 0);

        ++tmp[bin_number];
    }
    _hist = tmp;
}

void Histogram::join(Histogram& h) { // NOLINT
    auto num_bins = h._hist.size();
    carta::ApplyThreadLimit();
#pragma omp parallel for
    for (int i = 0; i < num_bins; i++) {
        h._hist[i] += _hist[i];
    }
}

void Histogram::setup_bins() {
    std::vector<int64_t> temp_bins;
    auto num_elements = _data.size();
    carta::ApplyThreadLimit();
#pragma omp parallel
    {
        auto num_threads = omp_get_num_threads();
        auto thread_index = omp_get_thread_num();
#pragma omp single
        { temp_bins.resize(_num_bins * num_threads); }
#pragma omp for
        for (int64_t i = 0; i < num_elements; i++) {
            auto val = _data[i];
            if (std::isfinite(val)) {
                int bin_number = std::clamp((int)((val - _min_val) / _bin_width), 0, _num_bins - 1);
                temp_bins[thread_index * _num_bins + bin_number]++;
            }
        }
#pragma omp for
        for (int64_t i = 0; i < _num_bins; i++) {
            for (int t = 0; t < num_threads; t++) {
                _hist[i] += temp_bins[_num_bins * t + i];
            }
        }
    }
}

HistogramResults Histogram::GetHistogram() const {
    HistogramResults results;
    results.num_bins = _num_bins;
    results.bin_width = _bin_width;
    results.bin_center = _min_val + (_bin_width / 2.0);
    results.histogram_bins = _hist;
    return results;
}
