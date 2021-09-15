/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Message.h"

void FillHistogramFromResults(CARTA::Histogram* histogram, const carta::BasicStats<float>& stats, const carta::Histogram& hist) {
    if (histogram == nullptr) {
        return;
    }
    histogram->set_num_bins(hist.GetNbins());
    histogram->set_bin_width(hist.GetBinWidth());
    histogram->set_first_bin_center(hist.GetBinCenter());
    *histogram->mutable_bins() = {hist.GetHistogramBins().begin(), hist.GetHistogramBins().end()};
    histogram->set_mean(stats.mean);
    histogram->set_std_dev(stats.stdDev);
}

void FillSpectralProfileDataMessage(CARTA::SpectralProfileData& profile_message, string& coordinate,
    vector<CARTA::StatsType>& required_stats, map<CARTA::StatsType, vector<double>>& spectral_data) {
    for (auto stats_type : required_stats) {
        // one SpectralProfile per stats type
        auto new_profile = profile_message.add_profiles();
        new_profile->set_coordinate(coordinate);
        new_profile->set_stats_type(stats_type);

        if (spectral_data.find(stats_type) == spectral_data.end()) { // stat not provided
            double nan_value = nan("");
            new_profile->set_raw_values_fp64(&nan_value, sizeof(double));
        } else {
            new_profile->set_raw_values_fp64(spectral_data[stats_type].data(), spectral_data[stats_type].size() * sizeof(double));
        }
    }
}

void FillStatisticsValuesFromMap(
    CARTA::RegionStatsData& stats_data, const vector<CARTA::StatsType>& required_stats, map<CARTA::StatsType, double>& stats_value_map) {
    // inserts values from map into message StatisticsValue field; needed by Frame and RegionDataHandler
    for (auto type : required_stats) {
        double value(0.0); // default
        auto carta_stats_type = static_cast<CARTA::StatsType>(type);
        if (stats_value_map.find(carta_stats_type) != stats_value_map.end()) { // stat found
            value = stats_value_map[carta_stats_type];
        } else { // stat not provided
            if (carta_stats_type != CARTA::StatsType::NumPixels) {
                value = nan("");
            }
        }

        // add StatisticsValue to message
        auto stats_value = stats_data.add_statistics();
        stats_value->set_stats_type(carta_stats_type);
        stats_value->set_value(value);
    }
}
