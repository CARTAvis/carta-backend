/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Histogram.h"

#include <fmt/format.h>
#include <omp.h>
#include <algorithm>
#include <cmath>

#include "Threading.h"

using namespace carta;

Histogram::Histogram(int num_bins, float min_value, float max_value, const std::vector<float>& data) {
    _histogram.num_bins = num_bins;
    _histogram.bin_width = (max_value - min_value) / num_bins;
    _min_val = min_value;
    _max_val = max_value;
    _histogram.histogram_bins.resize(num_bins);
    _histogram.bin_center = _min_val + (_histogram.bin_width * 0.5);

    Fill(data);
}

Histogram::Histogram(Histogram& h) {
    _histogram.num_bins = h.GetNbins();
    _histogram.bin_width = h.GetBinWidth();
    _histogram.bin_center = h.GetBinCenter();
    _min_val = h.GetMinVal();
    _max_val = h.GetMaxVal();
    _histogram.histogram_bins = h.GetHistogramBins();
}

bool Histogram::join(Histogram& h) { // NOLINT
    if (!ConsistencyCheck(*this, h)) {
        fmt::print("(debug) Consistency check failed to join histograms - won't join them\n");
        return false;
    }
    const int num_bins = h.GetHistogramBins().size();
    const auto& other_bins = h.GetHistogramBins();
    ThreadManager::ApplyThreadLimit();
#pragma omp parallel for
    for (int i = 0; i < num_bins; i++) {
        _histogram.histogram_bins[i] += other_bins[i];
    }
    return true;
}

void Histogram::Fill(const std::vector<float>& data) {
    std::vector<int64_t> temp_bins;
    auto num_elements = data.size();
    ThreadManager::ApplyThreadLimit();
#pragma omp parallel
    {
        auto num_threads = omp_get_num_threads();
        auto thread_index = omp_get_thread_num();
#pragma omp single
        { temp_bins.resize(_histogram.num_bins * num_threads); }
#pragma omp for
        for (int64_t i = 0; i < num_elements; i++) {
            auto val = data[i];
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

bool Histogram::ConsistencyCheck(const Histogram& a, const Histogram& b) {
    if (a.GetNbins() != b.GetNbins()) {
        std::printf("(debug) Histograms don't have the same number of bins: %d and %d\n", a.GetNbins(), b.GetNbins());
        return false;
    }
    if (fabs(a.GetMinVal() - b.GetMinVal()) > std::numeric_limits<float>::epsilon()) {
        std::printf("(debug) Lower histograms limits are not equal: %f and %f\n", a.GetMinVal(), b.GetMinVal());
        return false;
    }
    if (fabs(a.GetMaxVal() - b.GetMaxVal()) > std::numeric_limits<float>::epsilon()) {
        std::printf("(debug) Upper histograms limits are not equal: %f and %f\n", a.GetMaxVal(), b.GetMaxVal());
        return false;
    }
    return true;
}