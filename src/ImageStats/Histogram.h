/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGESTATS_HISTOGRAM_H_
#define CARTA_BACKEND_IMAGESTATS_HISTOGRAM_H_

#include <cstddef>
#include <vector>

namespace carta {

class Histogram {
    float _min_val;                   // lower bound of the histogram (inclusive)
    float _max_val;                   // upper bound of the histogram (inclusive)
    float _bin_width;                 // bin width
    float _bin_center;                // bin center
    std::vector<int> _histogram_bins; // histogram bin counts

    void Fill(const float* data, const size_t data_size);
    static bool ConsistencyCheck(const Histogram&, const Histogram&);

public:
    Histogram() = default; // required to create empty histograms used in references
    Histogram(int num_bins, float min_value, float max_value, const float* data, const size_t data_size);

    Histogram(const Histogram& h);

    bool Add(const Histogram& h);

    float GetMinVal() const {
        return _min_val;
    }
    float GetMaxVal() const {
        return _max_val;
    }
    size_t GetNbins() const {
        return _histogram_bins.size();
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
