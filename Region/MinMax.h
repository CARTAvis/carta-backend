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
    double _sum, _sum_squares;
    size_t _value_counter;
    const std::vector<T>& _data;

public:
    MinMax(const std::vector<T>& data);
    MinMax(MinMax& mm, tbb::split);

    void operator()(const tbb::blocked_range<size_t>& r);
    void join(MinMax& other); // NOLINT

    std::pair<T, T> GetMinMax() const;
    double GetMean() const;
    double GetSigma() const;
};

template <typename T>
MinMax<T>::MinMax(const std::vector<T>& data)
    : _min_val(std::numeric_limits<T>::max()), _max_val(std::numeric_limits<T>::lowest()), _sum(0), _sum_squares(0), _value_counter(0), _data(data) {}

template <typename T>
MinMax<T>::MinMax(MinMax<T>& mm, tbb::split)
    : _min_val(std::numeric_limits<T>::max()), _max_val(std::numeric_limits<T>::lowest()), _sum(0), _sum_squares(0), _value_counter(0), _data(mm._data) {}

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
            _value_counter++;
            _sum += val;
            _sum_squares += val * val;
        }
    }
    _min_val = t_min;
    _max_val = t_max;
}

template <typename T>
void MinMax<T>::join(MinMax<T>& other) { // NOLINT
    _min_val = std::min(_min_val, other._min_val);
    _max_val = std::max(_max_val, other._max_val);
    _value_counter += other._value_counter;
    _sum += other._sum;
    _sum_squares += other._sum_squares;
}

template <typename T>
std::pair<T, T> MinMax<T>::GetMinMax() const {
    return {_min_val, _max_val};
}

template <typename T>
double MinMax<T>::GetMean() const {
    if (_value_counter > 0) {
        return _sum / _value_counter;
    }
    return NAN;
}

template <typename T>
double MinMax<T>::GetSigma() const {
    if (_value_counter > 0) {
        double mean = GetMean();
        return sqrt(_sum_squares / _value_counter - mean * mean);
    }
    return NAN;
}


} // namespace carta

#endif // CARTA_BACKEND_REGION_MINMAX_H_
