#ifndef CARTA_BACKEND_REGION_MINMAX_H_
#define CARTA_BACKEND_REGION_MINMAX_H_

#include <algorithm>

#include <tbb/blocked_range2d.h>
#include <tbb/blocked_range3d.h>
#include <tbb/parallel_reduce.h>

#include <casacore/casa/Arrays/Array.h>

namespace carta {

template <typename T>
class MinMax {
    T _min_val, _max_val;
    const std::vector<T>& _data;

public:
    MinMax(const std::vector<T>& data);
    MinMax(MinMax& mm, tbb::split);

    void operator()(const tbb::blocked_range<size_t>& r);
    void join(MinMax& other); // NOLINT

    std::pair<T, T> GetMinMax() const;
};

template <typename T>
MinMax<T>::MinMax(const std::vector<T>& data)
    : _min_val(std::numeric_limits<T>::max()), _max_val(std::numeric_limits<T>::min()), _data(data) {}

template <typename T>
MinMax<T>::MinMax(MinMax<T>& mm, tbb::split)
    : _min_val(std::numeric_limits<T>::max()), _max_val(std::numeric_limits<T>::min()), _data(mm._data) {}

template <typename T>
void MinMax<T>::operator()(const tbb::blocked_range<size_t>& r) {
    T t_min = _min_val;
    T t_max = _max_val;
    for (size_t i = r.begin(); i != r.end(); ++i) {
        T val = _data[i];
        if (std::isfinite(val)) {
            if (val < t_min) {
                t_min = val;
            }
            if (val > t_max) {
                t_max = val;
            }
        }
    }
    _min_val = t_min;
    _max_val = t_max;
}

template <typename T>
void MinMax<T>::join(MinMax<T>& other) { // NOLINT
    _min_val = std::min(_min_val, other._min_val);
    _max_val = std::max(_max_val, other._max_val);
}

template <typename T>
std::pair<T, T> MinMax<T>::GetMinMax() const {
    return {_min_val, _max_val};
}

} // namespace carta

#endif // CARTA_BACKEND_REGION_MINMAX_H_
