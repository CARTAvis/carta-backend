#pragma once

#include <algorithm>
#include <casacore/casa/Arrays/Matrix.h>
#include <tbb/blocked_range2d.h>
#include <tbb/parallel_reduce.h>

namespace carta {

template <typename T>
class MinMax {
    T min, max;
    const casacore::Matrix<T> &chanMatrix;

public:
    MinMax(const casacore::Matrix<T> &cm);
    MinMax(MinMax &mm, tbb::split);

    void operator()(const tbb::blocked_range2d<size_t> &r);
    void join(MinMax &other);

    std::pair<T,T> getMinMax() const;
};

template <typename T>
MinMax<T>::MinMax(const casacore::Matrix<T> &cm)
    : min(std::numeric_limits<T>::max()),
      max(std::numeric_limits<T>::min()),
      chanMatrix(cm)
{}

template <typename T>
MinMax<T>::MinMax(MinMax<T> &mm, tbb::split)
    : min(std::numeric_limits<T>::max()),
      max(std::numeric_limits<T>::min()),
      chanMatrix(mm.chanMatrix)
{}

template <typename T>
void MinMax<T>::operator()(const tbb::blocked_range2d<size_t> &r) {
    T tmin = min;
    T tmax = max;
    for(size_t j = r.rows().begin(); j != r.rows().end(); ++j) {
        for(size_t i = r.cols().begin(); i != r.cols().end(); ++i) {
            T val = chanMatrix(i,j);
            if(std::isnan(val))
                continue;
            tmin = std::min(tmin, val);
            tmax = std::max(tmax, val);
        }
    }
    min = tmin;
    max = tmax;
}

template <typename T>
void MinMax<T>::join(MinMax<T> &other) {
    min = std::min(min, other.min);
    max = std::max(max, other.max);
}

template <typename T>
std::pair<T,T> MinMax<T>::getMinMax() const {
    return {min, max};
}


} // namespace carta
