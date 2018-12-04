#pragma once

#include <algorithm>
#include <casacore/casa/Arrays/Array.h>
#include <tbb/blocked_range2d.h>
#include <tbb/blocked_range3d.h>
#include <tbb/parallel_reduce.h>

namespace carta {

template <typename T>
class MinMax {
    T minval, maxval;
    const casacore::Array<T> &histArray;

public:
    MinMax(const casacore::Array<T> &hArray);
    MinMax(MinMax &mm, tbb::split);

    void operator()(const tbb::blocked_range2d<size_t> &r);
    void operator()(const tbb::blocked_range3d<size_t> &r);
    void join(MinMax &other);

    std::pair<T,T> getMinMax() const;
};

template <typename T>
MinMax<T>::MinMax(const casacore::Array<T> &hArray)
    : minval(std::numeric_limits<T>::max()),
      maxval(std::numeric_limits<T>::min()),
      histArray(hArray)
{}

template <typename T>
MinMax<T>::MinMax(MinMax<T> &mm, tbb::split)
    : minval(std::numeric_limits<T>::max()),
      maxval(std::numeric_limits<T>::min()),
      histArray(mm.histArray)
{}

template <typename T>
void MinMax<T>::operator()(const tbb::blocked_range2d<size_t> &r) {
    T tmin = minval;
    T tmax = maxval;
    for(size_t j = r.rows().begin(); j != r.rows().end(); ++j) {
        for(size_t i = r.cols().begin(); i != r.cols().end(); ++i) {
            T val = histArray(casacore::IPosition(2,i,j));
            if(std::isnan(val) || std::isinf(val))
                continue;
            tmin = std::min(tmin, val);
            tmax = std::max(tmax, val);
        }
    }
    minval = tmin;
    maxval = tmax;
}

template <typename T>
void MinMax<T>::operator()(const tbb::blocked_range3d<size_t> &r) {
    T tmin = minval;
    T tmax = maxval;
    for(size_t k = r.pages().begin(); k != r.pages().end(); ++k) {
        for(size_t j = r.rows().begin(); j != r.rows().end(); ++j) {
            for(size_t i = r.cols().begin(); i != r.cols().end(); ++i) {
                T val = histArray(casacore::IPosition(3,i,j,k));
                if(std::isnan(val) || std::isinf(val))
                    continue;
                tmin = std::min(tmin, val);
                tmax = std::max(tmax, val);
            }
        }
    }
    minval = tmin;
    maxval = tmax;
}

template <typename T>
void MinMax<T>::join(MinMax<T> &other) {
    minval = std::min(minval, other.minval);
    maxval = std::max(maxval, other.maxval);
}

template <typename T>
std::pair<T,T> MinMax<T>::getMinMax() const {
    return {minval, maxval};
}


} // namespace carta
