/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021, 2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# StatsCalculator.h: functions for calculating statistics and histograms

#ifndef CARTA_BACKEND_IMAGESTATS_STATSCALCULATOR_H_
#define CARTA_BACKEND_IMAGESTATS_STATSCALCULATOR_H_

#include <vector>

#include <casacore/images/Images/ImageInterface.h>

#include <carta-protobuf/enums.pb.h>
#include "BasicStatsCalculator.h"
#include "Histogram.h"

namespace carta {

void CalcBasicStats(BasicStats<float>& stats, const float* data, const size_t data_size);

Histogram CalcHistogram(int num_bins, const BasicStats<float>& stats, const float* data, const size_t data_size);

bool CalcStatsValues(std::map<CARTA::StatsType, std::vector<double>>& stats_values, const std::vector<CARTA::StatsType>& requested_stats,
    const casacore::ImageInterface<float>& image, bool per_channel = true);

} // namespace carta

#endif // CARTA_BACKEND_IMAGESTATS_STATSCALCULATOR_H_
