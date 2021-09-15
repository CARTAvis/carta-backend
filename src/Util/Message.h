#ifndef CARTA_BACKEND__UTIL_MESSAGE_H_
#define CARTA_BACKEND__UTIL_MESSAGE_H_

void FillHistogramFromResults(CARTA::Histogram* histogram, const carta::BasicStats<float>& stats, const carta::Histogram& hist);

void FillSpectralProfileDataMessage(CARTA::SpectralProfileData& profile_message, std::string& coordinate,
    std::vector<CARTA::StatsType>& required_stats, std::map<CARTA::StatsType, std::vector<double>>& spectral_data);

void FillStatisticsValuesFromMap(CARTA::RegionStatsData& stats_data, const std::vector<CARTA::StatsType>& required_stats,
    std::map<CARTA::StatsType, double>& stats_value_map);

#endif // CARTA_BACKEND__UTIL_MESSAGE_H_
