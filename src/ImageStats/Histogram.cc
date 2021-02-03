/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Histogram.h"

#include <algorithm>
#include <cmath>

#include "Threading.h"

using namespace carta;

Histogram::Histogram(int num_bins, float min_value, float max_value, const std::vector<float>& data) :_data(data) {
    _histogram.num_bins = num_bins;
    _histogram.bin_width =  (max_value - min_value) / num_bins;
    _min_val = min_value;
    _max_val = max_value;
    _histogram.histogram_bins.resize(num_bins);
    _histogram.bin_center = _min_val + (_histogram.bin_width * 0.5);
}

Histogram::Histogram(Histogram& h, tbb::split) : _data(h.GetData()) {
    _histogram.num_bins = h.GetNbins();
    _histogram.bin_width = h.GetBinWidth();
    _min_val = h.GetMinVal();
    _max_val = h.GetMaxVal();
    _histogram.histogram_bins = h.GetHistogramBins();
}

void Histogram::operator()(const tbb::blocked_range<size_t>& r) {
    std::vector<int> tmp(_histogram.histogram_bins);
    for (auto i = r.begin(); i != r.end(); ++i) {
        auto v = _data[i];
        if (std::isnan(v) || std::isinf(v)) {
            continue;
        }
        size_t bin_number = std::max<size_t>(std::min<size_t>((size_t)((v - _min_val) / _histogram.bin_width), (size_t)_histogram.histogram_bins.size() - 1), 0);
        ++tmp[bin_number];
    }
    _histogram.histogram_bins = tmp;
}

void Histogram::join(Histogram& h) { // NOLINT
    auto num_bins = h._hist.size();
    ThreadManager::ApplyThreadLimit();
#pragma omp parallel for
    for (int i = 0; i < num_bins; i++) {
        _histogram.histogram_bins[i] += h.GetHistogramBins()[i];
    }
    return true;
}

void Histogram::setup_bins() {
    std::vector<int64_t> temp_bins;
    auto num_elements = _data.size();
    ThreadManager::ApplyThreadLimit();
#pragma omp parallel
    {
        auto num_threads = omp_get_num_threads();
        auto thread_index = omp_get_thread_num();
#pragma omp single
        { temp_bins.resize(_histogram.num_bins * num_threads); }
#pragma omp for
        for (int64_t i = 0; i < num_elements; i++) {
            auto val = _data[i];
            if (_min_val <= val && val <= _max_val) {
                int bin_number = std::clamp((int)((val - _min_val) / _histogram.bin_width), 0, _histogram.num_bins - 1);
                temp_bins[thread_index * _histogram.num_bins + bin_number]++;
            }
        }
#pragma omp for
        for (int64_t i = 0; i < _histogram.num_bins; i++) {
            for (int t = 0; t < num_threads; t++) {
                _histogram.histogram_bins[i] += temp_bins[_histogram.num_bins * t + i];
            }
        }
    }
}
