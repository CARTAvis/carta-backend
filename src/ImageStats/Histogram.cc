/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Histogram.h"

#include <omp.h>
#include <algorithm>
#include <cmath>

#include "Logger/Logger.h"
#include "ThreadingManager/ThreadingManager.h"

using namespace carta;

Histogram::Histogram(int num_bins, const Bounds<float>& bounds, const float* data, const size_t data_size)
    : _bin_width((bounds.max - bounds.min) / num_bins),
      _min_val(bounds.min),
      _max_val(bounds.max),
      _bin_center(bounds.min + (_bin_width * 0.5)),
      _histogram_bins(num_bins, 0) {
    Fill(data, data_size);
}

Histogram::Histogram(const Histogram& h)
    : _bin_width(h.GetBinWidth()),
      _bin_center(h.GetBinCenter()),
      _min_val(h.GetMinVal()),
      _max_val(h.GetMaxVal()),
      _histogram_bins(h.GetHistogramBins()) {}

bool Histogram::Add(const Histogram& h) {
    if (!ConsistencyCheck(*this, h)) {
        spdlog::warn("Could not join histograms: consistency check failed.");
        return false;
    }
    const int num_bins = h.GetHistogramBins().size();
    const auto& other_bins = h.GetHistogramBins();
#pragma omp simd
    for (int i = 0; i < num_bins; i++) {
        _histogram_bins[i] = _histogram_bins[i] + other_bins[i];
    }
    return true;
}

void Histogram::Fill(const float* data, const size_t data_size) {
    std::vector<int64_t> temp_bins;
    const auto num_elements = data_size;
    const size_t num_bins = GetNbins();
    ThreadManager::ApplyThreadLimit();
#pragma omp parallel
    {
        auto num_threads = omp_get_num_threads();
        auto thread_index = omp_get_thread_num();
#pragma omp single
        { temp_bins.resize(num_bins * num_threads); }
#pragma omp for
        for (int64_t i = 0; i < num_elements; i++) {
            auto val = data[i];
            if (_min_val <= val && val <= _max_val) {
                size_t bin_number = std::clamp((size_t)((val - _min_val) / _bin_width), (size_t)0, num_bins - 1);
                temp_bins[thread_index * num_bins + bin_number]++;
            }
        }
#pragma omp for
        for (int64_t i = 0; i < num_bins; i++) {
            for (int t = 0; t < num_threads; t++) {
                _histogram_bins[i] += temp_bins[num_bins * t + i];
            }
        }
    }
}

bool Histogram::ConsistencyCheck(const Histogram& a, const Histogram& b) {
    if (a.GetNbins() != b.GetNbins()) {
        spdlog::warn("Histograms don't have the same number of bins: {} and {}", a.GetNbins(), b.GetNbins());
        return false;
    }
    if (a.GetBounds() != b.GetBounds()) {
        spdlog::warn("Histogram bounds are not equal: [{}, {}] and [{}, {}]", a.GetMinVal(), a.GetMaxVal(), b.GetMinVal(), b.GetMaxVal());
        return false;
    }
    return true;
}
void Histogram::SetHistogramBins(const std::vector<int>& bins) {
    if (bins.size() != _histogram_bins.size()) {
        spdlog::error("Could not reset histogram counts: vector sizes are not equal.");
    }
    _histogram_bins = bins;
}
