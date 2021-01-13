/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGESTATS_HISTOGRAM_H_
#define CARTA_BACKEND_IMAGESTATS_HISTOGRAM_H_

#include <vector>

#include <tbb/blocked_range2d.h>
#include <tbb/blocked_range3d.h>

namespace carta {

struct HistogramResults {
    int num_bins;
    float bin_width;
    float bin_center;
    std::vector<int> histogram_bins;
};

class Histogram {
    int _num_bins;
    float _bin_width;
    float _min_val;
    std::vector<int> _hist;
    const std::vector<float>& _data;

public:
    Histogram(int num_bins, float min_value, float max_value, const std::vector<float>& data);
    Histogram(Histogram& h, tbb::split);

    void operator()(const tbb::blocked_range<size_t>& r);
    void join(Histogram& h); // NOLINT
    void setup_bins();

    HistogramResults GetHistogram() const;
};

} // namespace carta

#endif // CARTA_BACKEND_IMAGESTATS_HISTOGRAM_H_
