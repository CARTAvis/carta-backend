#include "Histogram.h"

#include <algorithm>

#include <tbb/parallel_for.h>
#include <tbb/parallel_reduce.h>

using namespace carta;

Histogram::Histogram(int num_bins, float min_value, float max_value, const std::vector<float>& data)
    : _bin_width((max_value - min_value) / num_bins), _min_val(min_value), _hist(num_bins, 0), _data(data) {}

Histogram::Histogram(Histogram& h, tbb::split) : _bin_width(h._bin_width), _min_val(h._min_val), _hist(h._hist.size(), 0), _data(h._data) {}

void Histogram::operator()(const tbb::blocked_range<size_t>& r) {
    std::vector<int> tmp(_hist);
    for (auto i = r.begin(); i != r.end(); ++i) {
        auto v = _data[i];
        if (std::isnan(v) || std::isinf(v))
            continue;
        int bin = std::max(std::min((int)((v - _min_val) / _bin_width), (int)_hist.size() - 1), 0);
        ++tmp[bin];
    }
    _hist = tmp;
}

void Histogram::join(Histogram& h) {
    auto range = tbb::blocked_range<size_t>(0, _hist.size());
    auto loop = [this, &h](const tbb::blocked_range<size_t>& r) {
        size_t beg = r.begin();
        size_t end = r.end();
        std::transform(&h._hist[beg], &h._hist[end], &_hist[beg], &_hist[beg], std::plus<int>());
    };
    tbb::parallel_for(range, loop);
}
