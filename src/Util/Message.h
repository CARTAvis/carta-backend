/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__UTIL_MESSAGE_H_
#define CARTA_BACKEND__UTIL_MESSAGE_H_

#include <carta-protobuf/region_stats.pb.h>
#include <carta-protobuf/spectral_profile.pb.h>

#include "ImageStats/BasicStatsCalculator.h"
#include "ImageStats/Histogram.h"

void FillHistogramFromResults(CARTA::Histogram* histogram, const carta::BasicStats<float>& stats, const carta::Histogram& hist);

void FillSpectralProfileDataMessage(CARTA::SpectralProfileData& profile_message, std::string& coordinate,
    std::vector<CARTA::StatsType>& required_stats, std::map<CARTA::StatsType, std::vector<double>>& spectral_data);

void FillStatisticsValuesFromMap(CARTA::RegionStatsData& stats_data, const std::vector<CARTA::StatsType>& required_stats,
    std::map<CARTA::StatsType, double>& stats_value_map);

#endif // CARTA_BACKEND__UTIL_MESSAGE_H_
