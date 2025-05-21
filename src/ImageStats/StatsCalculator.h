/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# StatsCalculator.h: functions for calculating statistics and histograms

#ifndef CARTA_SRC_IMAGESTATS_STATSCALCULATOR_H_
#define CARTA_SRC_IMAGESTATS_STATSCALCULATOR_H_

#include <vector>

#include <casacore/images/Images/ImageInterface.h>
#include <casacore/images/Images/ImageStatistics.h>

#include <carta-protobuf/enums.pb.h>
#include "BasicStatsCalculator.h"
#include "Cache/RequirementsCache.h"

namespace carta {

static std::unordered_map<CARTA::StatsType, casacore::LatticeStatsBase::StatisticsTypes> carta_stats_to_casacore{
    {CARTA::StatsType::Sum, casacore::LatticeStatsBase::SUM}, {CARTA::StatsType::Mean, casacore::LatticeStatsBase::MEAN},
    {CARTA::StatsType::RMS, casacore::LatticeStatsBase::RMS}, {CARTA::StatsType::Sigma, casacore::LatticeStatsBase::SIGMA},
    {CARTA::StatsType::SumSq, casacore::LatticeStatsBase::SUMSQ}, {CARTA::StatsType::Min, casacore::LatticeStatsBase::MIN},
    {CARTA::StatsType::Extrema, casacore::LatticeStatsBase::MIN}, {CARTA::StatsType::Max, casacore::LatticeStatsBase::MAX}};

void CalcBasicStats(BasicStats<float>& stats, const float* data, const size_t data_size);

Histogram CalcHistogram(int num_bins, const HistogramBounds& bounds, const float* data, const size_t data_size);

bool CalcStatsValues(std::map<CARTA::StatsType, std::vector<double>>& stats_values, const std::vector<CARTA::StatsType>& requested_stats,
    const casacore::ImageInterface<float>& image, bool per_channel = true);

void GetPositionStats(const casacore::ImageInterface<float>& image, casacore::ImageStatistics<float> image_stats,
    CARTA::StatsType carta_stats_type, std::vector<double>& dbl_result);

bool ComputeFluxDensity(
    const casacore::ImageInterface<float>& image, casacore::ImageStatistics<float> image_stats, std::vector<double>& result);
bool GetBeamArea(const casacore::ImageInterface<float>& image, const casacore::String unit, double& beam_area);

} // namespace carta

#endif // CARTA_SRC_IMAGESTATS_STATSCALCULATOR_H_
