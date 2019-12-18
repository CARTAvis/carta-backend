#include "Histogram.h"

#include <algorithm>

#include <omp.h>
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

void Histogram::join(Histogram& h) { // NOLINT
    int iam, nt, ipoints;
    size_t beg, end;
#pragma omp parallel default(shared) private(iam, nt, ipoints, beg, end)
    {
        iam = omp_get_thread_num();
        nt = omp_get_num_threads();
        ipoints = _hist.size() / nt;
        beg = iam * ipoints;
        end = beg + ipoints - 1;
        std::transform(&h._hist[beg], &h._hist[end], &_hist[beg], &_hist[beg], std::plus<int>());
    }
}
