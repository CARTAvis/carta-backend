//# RegionStats.h: class for region statistics and histograms

#ifndef CARTA_BACKEND_REGION_REGIONSTATS_H_
#define CARTA_BACKEND_REGION_REGIONSTATS_H_

#include <unordered_map>
#include <vector>

#include <casacore/images/Images/ImageInterface.h>

#include <carta-protobuf/defs.pb.h>
#include <carta-protobuf/region_requirements.pb.h>
#include <carta-protobuf/region_stats.pb.h>

#include "../ImageStats/BasicStatsCalculator.h"
#include "../ImageStats/StatsCalculator.h"

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
    void CalcRegionBasicStats(int channel, int stokes, const std::vector<float>& data, BasicStats<float>& stats);
    // Histogram data
    bool GetHistogram(int channel, int stokes, int num_bins, CARTA::Histogram& histogram);
    void SetHistogram(int channel, int stokes, CARTA::Histogram& histogram);
    void CalcRegionHistogram(int channel, int stokes, int num_bins, const BasicStats<float>& stats, const std::vector<float>& data,
        CARTA::Histogram& histogram_msg);

    // Stats
    void SetStatsRequirements(const std::vector<int>& stats_types);
    size_t NumStats();
    // Stats data
    void FillStatsData(CARTA::RegionStatsData& stats_data, const casacore::ImageInterface<float>& image, int channel, int stokes);
    void FillStatsData(CARTA::RegionStatsData& stats_data, std::map<CARTA::StatsType, double>& stats_values);
    bool CalcRegionStats(std::map<CARTA::StatsType, std::vector<double>>& stats_values, const std::vector<int>& requested_stats,
        const casacore::ImageInterface<float>& image);

    // invalidate stored histogram and statistics calculations for previous region settings
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
