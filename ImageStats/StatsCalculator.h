//# StatsCalculator.h: functions for calculating statistics and histograms

#ifndef CARTA_BACKEND_IMAGESTATS_STATSCALCULATOR_H_
#define CARTA_BACKEND_IMAGESTATS_STATSCALCULATOR_H_

#include <vector>

#include <casacore/images/Images/ImageInterface.h>

#include <carta-protobuf/enums.pb.h>
#include "BasicStatsCalculator.h"
#include "Histogram.h"

using namespace carta;

void CalcBasicStats(const std::vector<float>& data, BasicStats<float>& stats);

void CalcHistogram(int num_bins, const BasicStats<float>& stats, const std::vector<float>& data, HistogramResults& results);

bool CalcStatsValues(std::map<CARTA::StatsType, std::vector<double>>& stats_values, const std::vector<int>& requested_stats,
    const casacore::ImageInterface<float>& image, bool per_channel = true);

#endif // CARTA_BACKEND_IMAGESTATS_STATSCALCULATOR_H_
