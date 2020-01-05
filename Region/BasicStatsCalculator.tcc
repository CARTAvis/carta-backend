#ifndef CARTA_BACKEND_REGION_BASICSTATSCALCULATOR_TCC_
#define CARTA_BACKEND_REGION_BASICSTATSCALCULATOR_TCC_

#include "BasicStatsCalculator.h"

namespace carta {

template<typename T>
void BasicStats<T>::join(BasicStats<T>& other) {
    if (other.num_pixels) {
        sum += other.sum;
        sumSq += other.sumSq;
        num_pixels += other.num_pixels;
        min_val = std::min(min_val, other.min_val);
        max_val = std::max(max_val, other.max_val);
        mean = sum / num_pixels;
        stdDev = num_pixels > 1 ? sqrt((sumSq - (sum * sum / num_pixels)) / (num_pixels - 1)): NAN;
        rms = sqrt(sumSq / num_pixels);
    }
}

template<typename T>
BasicStats<T>::BasicStats(size_t num_pixels, double sum, double mean, double stdDev, T min_val, T max_val, double rms, double sumSq)
    : num_pixels(num_pixels), sum(sum), mean(mean), stdDev(stdDev), min_val(min_val), max_val(max_val), rms(rms), sumSq(sumSq){
}
template<typename T>
BasicStats<T>::BasicStats()
    : num_pixels(0), sum(0), mean(0), stdDev(0), min_val(std::numeric_limits<T>::max()), max_val(std::numeric_limits<T>::lowest()), rms(0), sumSq(0) {
}

template<typename T>
BasicStatsCalculator<T>::BasicStatsCalculator(const std::vector<T>& data)
    : _min_val(std::numeric_limits<T>::max()), _max_val(std::numeric_limits<T>::lowest()), _sum(0), _sum_squares(0), _num_pixels(0), _data(data) {}

template<typename T>
BasicStatsCalculator<T>::BasicStatsCalculator(BasicStatsCalculator<T>& mm, tbb::split)
    : _min_val(std::numeric_limits<T>::max()), _max_val(std::numeric_limits<T>::lowest()), _sum(0), _sum_squares(0), _num_pixels(0), _data(mm._data) {}

template<typename T>
void BasicStatsCalculator<T>::operator()(const tbb::blocked_range<size_t>& r) {
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
            _num_pixels++;
            _sum += val;
            _sum_squares += val * val;
        }
    }
    _min_val = t_min;
    _max_val = t_max;
}

template<typename T>
void BasicStatsCalculator<T>::reduce(const int start, const int end) {
	  int i;
#pragma omp parallel for private(i) shared(_data) reduction(min: _min_val) reduction(max:_max_val) reduction(+:_num_pixels) reduction(+:_sum) reduction(+:_sum_squares)
    for (i = start; i < end ; i++) {
        T val = _data[i];
        if (std::isfinite(val)) {
            if (val < _min_val) {
                _min_val = val;
            }
			if (val > _max_val) {
                _max_val = val;
            }
            _num_pixels++;
            _sum += val;
            _sum_squares += val * val;
        }
    }
}


template<typename T>
void BasicStatsCalculator<T>::join(BasicStatsCalculator<T>& other) { // NOLINT
    _min_val = std::min(_min_val, other._min_val);
    _max_val = std::max(_max_val, other._max_val);
    _num_pixels += other._num_pixels;
    _sum += other._sum;
    _sum_squares += other._sum_squares;
}

template<typename T>
BasicStats<T> BasicStatsCalculator<T>::GetStats() const {
    double mean;
    double stdDev;
    double rms;

    if (_num_pixels > 0) {
        mean = _sum / _num_pixels;
        stdDev = _num_pixels > 1 ? sqrt((_sum_squares - (_sum * _sum / _num_pixels)) / (_num_pixels - 1)): NAN;
        rms = sqrt(_sum_squares / _num_pixels);
    } else {
        mean = NAN;
        stdDev = NAN;
        rms = NAN;
    }

    return BasicStats<T>{
        _num_pixels,
        _sum,
        mean,
        stdDev,
        _min_val,
        _max_val,
        rms,
        _sum_squares
    };
}

} // namespace carta

#endif //CARTA_BACKEND_REGION_BASICSTATSCALCULATOR_TCC_
