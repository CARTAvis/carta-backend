#ifndef CARTA_BACKEND_REGION_HISTOGRAM_H_
#define CARTA_BACKEND_REGION_HISTOGRAM_H_

#include <vector>

#include <tbb/blocked_range2d.h>
#include <tbb/blocked_range3d.h>

#include <casacore/casa/Arrays/Matrix.h>

namespace carta {

class Histogram {
    float _bin_width;
    float _min_val;
    std::vector<int> _hist;
    const std::vector<float>& _data;

public:
    Histogram(int num_bins, float min_value, float max_value, const std::vector<float>& data);
    Histogram(Histogram& h, tbb::split);

    void operator()(const tbb::blocked_range<size_t>& r);
    void join(Histogram& h); // NOLINT
    void setup_bins(const int start, const int end);

    float GetBinWidth() const {
        return _bin_width;
    }

    std::vector<int> GetHistogram() const {
        return _hist;
    }
};

} // namespace carta

#endif // CARTA_BACKEND_REGION_HISTOGRAM_H_
