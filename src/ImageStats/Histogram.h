/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
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
    std::vector<int> histogram_bins; // histogram bin counts
};

class Histogram {
    float _min_val;              // lower bound of the histogram (inclusive)
    float _max_val;              // upper bound of the histogram (inclusive)
    int _num_bins;
    float _bin_width;
    float _bin_center;
    std::vector<int> _histogram_bins; // histogram bin counts

    void Fill(const std::vector<float>&);
    static bool ConsistencyCheck(const Histogram&, const Histogram&);

public:
    Histogram() = default; // required to create empty histograms used in references
    Histogram(int num_bins, float min_value, float max_value, const std::vector<float>& data);

    Histogram(const Histogram& h);

    bool join(const Histogram& h); // NOLINT

    float GetMinVal() const {
        return _min_val;
    }
    float GetMaxVal() const {
        return _max_val;
    }
    int GetNbins() const {
        return _num_bins;
    }
    float GetBinWidth() const {
        return _bin_width;
    }
    float GetBinCenter() const {
        return _bin_center;
    }
    const std::vector<int>& GetHistogramBins() const {
        return _histogram_bins;
    }

    void SetHistogramBins(const std::vector<int>&);
};

} // namespace carta

#endif // CARTA_BACKEND_IMAGESTATS_HISTOGRAM_H_
