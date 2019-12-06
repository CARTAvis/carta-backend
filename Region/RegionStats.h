//# RegionStats.h: class for calculating region statistics and histograms

#ifndef CARTA_BACKEND_REGION_REGIONSTATS_H_
#define CARTA_BACKEND_REGION_REGIONSTATS_H_

#include <unordered_map>
#include <vector>

#include <casacore/images/Images/ImageInterface.h>

#include <carta-protobuf/defs.pb.h>                // Histogram, StatisticsValue
#include <carta-protobuf/region_requirements.pb.h> // HistogramConfig
#include <carta-protobuf/region_stats.pb.h>        // RegionStatsData
#include "BasicStatsCalculator.h"

namespace carta {

class RegionStats {
public:
    RegionStats();

    // Histograms
    // config
    bool SetHistogramRequirements(const std::vector<CARTA::SetHistogramRequirements_HistogramConfig>& histogram_reqs);
    size_t NumHistogramConfigs();
    CARTA::SetHistogramRequirements_HistogramConfig GetHistogramConfig(int histogram_index);
    // basic stats
    bool GetBasicStats(int channel, int stokes, BasicStats<float>& stats);
    void SetBasicStats(int channel, int stokes, const BasicStats<float>& stats);
    void CalcBasicStats(int channel, int stokes, const std::vector<float>& data, BasicStats<float>& stats);
    // CARTA::Histogram
    bool GetHistogram(int channel, int stokes, int num_bins, CARTA::Histogram& histogram);
    void SetHistogram(int channel, int stokes, CARTA::Histogram& histogram);
    void CalcHistogram(int channel, int stokes, int num_bins, const BasicStats<float>& stats, const std::vector<float>& data,
        CARTA::Histogram& histogram_msg);

    // Stats
    void SetStatsRequirements(const std::vector<int>& stats_types);
    size_t NumStats();
    void FillStatsData(CARTA::RegionStatsData& stats_data, const casacore::ImageInterface<float>& image, int channel, int stokes);
    void FillStatsData(CARTA::RegionStatsData& stats_data, std::map<CARTA::StatsType, double>& stats_values);
    bool CalcStatsValues(std::map<CARTA::StatsType, std::vector<double>>& stats_values, const std::vector<int>& requested_stats,
        const casacore::ImageInterface<float>& image, bool per_channel = true);

    // invalidate stored calculations for previous region settings
    void ClearStats();

private:
    // Valid calculations
    bool _histograms_valid, _stats_valid;

    // Histogram config
    std::vector<CARTA::SetHistogramRequirements_HistogramConfig> _histogram_reqs;
    // Statistics config
    std::vector<int> _stats_reqs; // CARTA::StatsType requirements for this region

    // Cache calculations
    // BasicStats: first key is stokes, second is channel number (-2 all channels for cube)
    std::unordered_map<int, std::unordered_map<int, BasicStats<float>>> _basic_stats;
    // Histogram: first key is stokes, second is channel number (-2 all channels for cube)
    std::unordered_map<int, std::unordered_map<int, CARTA::Histogram>> _histograms;
    // Region stats: first key is stokes, second is channel number
    std::unordered_map<int, std::unordered_map<int, std::vector<double>>> _stats_data;
};

} // namespace carta
#endif // CARTA_BACKEND_REGION_REGIONSTATS_H_
