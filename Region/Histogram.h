#pragma once

#include <casacore/casa/Arrays/Matrix.h>
#include <tbb/blocked_range2d.h>
#include <tbb/blocked_range3d.h>
#include <vector>

namespace carta {

class Histogram {
    float binWidth;
    float minVal;
    std::vector<int> hist;
    const std::vector<float> &data;

public:
    Histogram(int numBins, float minValue, float maxValue, const std::vector<float> &data_);
    Histogram(Histogram &h, tbb::split);

    void operator()(const tbb::blocked_range<size_t> &r);
    void join(Histogram &h);

    float getBinWidth() const {
        return binWidth;
    }

    std::vector<int> getHistogram() const {
        return hist;
    }
};

} // namespace carta
