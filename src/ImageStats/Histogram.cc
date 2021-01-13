/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Histogram.h"

#include <algorithm>
#include <cmath>

#include <omp.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_reduce.h>

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
    auto range = tbb::blocked_range<size_t>(0, _hist.size());
    auto loop = [this, &h](const tbb::blocked_range<size_t>& r) {
        size_t beg = r.begin();
        size_t end = r.end();
        std::transform(&h._hist[beg], &h._hist[end], &_hist[beg], &_hist[beg], std::plus<int>());
    };
    tbb::parallel_for(range, loop);
}

void Histogram::setup_bins() {
    int64_t omp_index, stride, buckets;
    int** bins_bin;

    auto calc_lambda = [&](size_t start, size_t lstride) {
        int* lbins = new int[_hist.size()];
        size_t end = std::min((size_t)(start + lstride), _data.size());
        memset(lbins, 0, _hist.size() * sizeof(int));
        for (size_t i = start; i < end; i++) {
            auto v = _data[i];
            if (std::isfinite(v)) {
                size_t bin_number = std::max<size_t>(std::min<size_t>((size_t)((v - _min_val) / _bin_width), (size_t)_hist.size() - 1), 0);
                ++lbins[bin_number];
            }
        }
        return lbins;
    };
#pragma omp parallel
#pragma omp single
    { buckets = omp_get_num_threads(); }
#pragma omp single
    {
        stride = _data.size() / buckets + 1;
        bins_bin = new int*[buckets + 1];
    }
#pragma omp parallel for
    for (omp_index = 0; omp_index < buckets; omp_index++) {
        bins_bin[omp_index] = calc_lambda(omp_index * stride, stride);
    }
    stride = 1;
    do {
#pragma omp single
        {
            for (omp_index = 0; omp_index <= (buckets - stride * 2); omp_index += stride * 2) {
#pragma omp task
                std::transform((bins_bin[omp_index + stride]), &(bins_bin[omp_index + stride][_hist.size()]), (bins_bin[omp_index]),
                    (bins_bin[omp_index]), std::plus<int>());
            }
            stride *= 2;
        }
#pragma omp taskwait
    } while (stride <= buckets / 2);
    for (omp_index = 0; omp_index < _hist.size(); omp_index++) {
        _hist[omp_index] = bins_bin[0][omp_index];
    }
    for (omp_index = 0; omp_index < buckets; omp_index++) {
        delete[] bins_bin[omp_index];
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
