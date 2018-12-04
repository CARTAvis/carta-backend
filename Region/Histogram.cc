#include "Histogram.h"
#include <algorithm>
#include <tbb/parallel_for.h>
#include <tbb/parallel_reduce.h>

using namespace carta;

Histogram::Histogram(int numBins, float minValue, float maxValue, const casacore::Array<float> &hArray)
    : binWidth((maxValue - minValue)/numBins),
      minVal(minValue),
      hist(numBins, 0),
      histArray(hArray)
{}

Histogram::Histogram(Histogram &h, tbb::split)
    : binWidth(h.binWidth),
      minVal(h.minVal),
      hist(h.hist.size(), 0),
      histArray(h.histArray)
{}

void Histogram::operator()(const tbb::blocked_range2d<size_t> &r) {
    std::vector<int> tmp(hist);
    for (auto j = r.rows().begin(); j != r.rows().end(); ++j) {
        for (auto i = r.cols().begin(); i != r.cols().end(); ++i) {
            auto v = histArray(casacore::IPosition(2,i,j));
            if (std::isnan(v) || std::isinf(v))
                continue;
            int bin = std::max(std::min((int) ((v - minVal) / binWidth), (int)hist.size() - 1), 0);
            ++tmp[bin];
        }
    }
    hist = tmp;
}

void Histogram::operator()(const tbb::blocked_range3d<size_t> &r) {
    std::vector<int> tmp(hist);
    for (auto k = r.pages().begin(); k != r.pages().end(); ++k) {
        for (auto j = r.rows().begin(); j != r.rows().end(); ++j) {
            for (auto i = r.cols().begin(); i != r.cols().end(); ++i) {
                auto v = histArray(casacore::IPosition(3,i,j,k));
                if (std::isnan(v) || std::isinf(v))
                    continue;
                int bin = std::max(std::min((int) ((v - minVal) / binWidth), (int)hist.size() - 1), 0);
                ++tmp[bin];
            }
        }
    }
    hist = tmp;
}

void Histogram::join(Histogram &h) {
    auto range = tbb::blocked_range<size_t>(0, hist.size());
    auto loop = [this, &h](const tbb::blocked_range<size_t> &r) {
        size_t beg = r.begin();
        size_t end = r.end();
        std::transform(&h.hist[beg], &h.hist[end], &hist[beg], &hist[beg], std::plus<int>());
    };
    tbb::parallel_for(range, loop);
}
