#include "Histogram.h"

#include <algorithm>

#include <omp.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_reduce.h>

using namespace carta;

Histogram::Histogram(int num_bins, float min_value, float max_value, const std::vector<float>& data)
    : _bin_width((max_value - min_value) / num_bins), _min_val(min_value), _hist(num_bins, 0), _data(data) {}

Histogram::Histogram(Histogram& h, tbb::split) : _bin_width(h._bin_width), _min_val(h._min_val), _hist(h._hist.size(), 0), _data(h._data) {}

void Histogram::operator()(const tbb::blocked_range<uint64_t>& r) {
    std::vector<uint64_t> tmp(_hist);
    for (auto i = r.begin(); i != r.end(); ++i) {
        auto v = _data[i];
        if (std::isnan(v) || std::isinf(v))
            continue;
        // int bin = std::max(std::min((int)((v - _min_val) / _bin_width), (int)_hist.size() - 1), 0);
        uint64_t _1 = (v - _min_val) / _bin_width;
        uint64_t _2 = _hist.size() - 1;
        uint64_t _3 = _1 < _2 ? _1 : _2;
        uint64_t bin = _3 >= 0 ? _3 : 0;
        ++tmp[bin];
    }
    _hist = tmp;
}

void Histogram::join(Histogram& h) { // NOLINT
    auto range = tbb::blocked_range<uint64_t>(0, _hist.size());
    auto loop = [this, &h](const tbb::blocked_range<uint64_t>& r) {
        size_t beg = r.begin();
        size_t end = r.end();
        std::transform(&h._hist[beg], &h._hist[end], &_hist[beg], &_hist[beg], std::plus<uint64_t>());
    };
    tbb::parallel_for(range, loop);
}

void Histogram::setup_bins(const uint64_t start, const uint64_t end) {
    uint64_t i, stride, buckets;
    uint64_t** bins_bin;

    auto calc_lambda = [&](uint64_t start, uint64_t lstride) {
        uint64_t* lbins = new uint64_t[_hist.size()];
        uint64_t end = std::min((size_t)(start + lstride), _data.size());
        memset(lbins, 0, _hist.size() * sizeof(int));
        for (uint64_t i = start; i < end; i++) {
            auto v = _data[i];
            if (std::isfinite(v)) {
                uint64_t _1 = (v - _min_val) / _bin_width;
                uint64_t _2 = _hist.size() - 1;
                uint64_t _3 = _1 < _2 ? _1 : _2;
                uint64_t binN = _3 >= 0 ? _3 : 0;
                ++lbins[binN];
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
        bins_bin = new uint64_t*[buckets + 1];
    }
#pragma omp parallel for
    for (i = 0; i < buckets; i++) {
        bins_bin[i] = calc_lambda(i * stride, stride);
    }
    stride = 1;
    do {
#pragma omp single
        {
            for (i = 0; i <= (buckets - stride * 2); i += stride * 2) {
#pragma omp task
                std::transform(
                    (bins_bin[i + stride]), &(bins_bin[i + stride][_hist.size()]), (bins_bin[i]), (bins_bin[i]), std::plus<int>());
            }
            stride *= 2;
        }
#pragma omp taskwait
    } while (stride <= buckets / 2);
    for (i = 0; i < _hist.size(); i++) {
        _hist[i] = bins_bin[0][i];
    }
    for (i = 0; i < buckets; i++) {
        delete[] bins_bin[i];
    }
}
