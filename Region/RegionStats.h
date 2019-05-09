//# RegionStats.h: class for calculating region statistics and histograms

#ifndef CARTA_BACKEND_REGION_REGIONSTATS_H_
#define CARTA_BACKEND_REGION_REGIONSTATS_H_

#include <unordered_map>
#include <vector>

#include <casacore/images/Images/ImageStatistics.h>

#include <carta-protobuf/defs.pb.h>                // Histogram, StatisticsValue
#include <carta-protobuf/region_requirements.pb.h> // HistogramConfig
#include <carta-protobuf/region_stats.pb.h>        // RegionStatsData

namespace carta {

class RegionStats {
public:
    RegionStats();
    ~RegionStats();

    // Histograms
    // config
    bool setHistogramRequirements(const std::vector<CARTA::SetHistogramRequirements_HistogramConfig>& histogramReqs);
    size_t numHistogramConfigs();
    CARTA::SetHistogramRequirements_HistogramConfig getHistogramConfig(int histogramIndex);
    // min max
    using minmax_t = std::pair<float, float>; // <min, max>
    bool getMinMax(int channel, int stokes, float& minVal, float& maxVal);
    void setMinMax(int channel, int stokes, minmax_t minmaxVals);
    void calcMinMax(int channel, int stokes, const std::vector<float>& data, float& minVal, float& maxVal);
    // CARTA::Histogram
    bool getHistogram(int channel, int stokes, int nbins, CARTA::Histogram& histogram);
    void setHistogram(int channel, int stokes, CARTA::Histogram& histogram);
    void calcHistogram(
        int channel, int stokes, int nBins, float minVal, float maxVal, const std::vector<float>& data, CARTA::Histogram& histogramMsg);

    // Stats
    void setStatsRequirements(const std::vector<int>& statsTypes);
    size_t numStats();
    void fillStatsData(CARTA::RegionStatsData& statsData, const casacore::ImageInterface<float>& image, int channel, int stokes);
    bool calcStatsValues(std::vector<std::vector<double>>& statsValues, const std::vector<int>& requestedStats,
        const casacore::ImageInterface<float>& image, bool perChannel = true);

    // invalidate stored calculations for previous region settings
    void clearStats();

private:
    // Valid calculations
    bool m_histogramsValid, m_statsValid;

    // Histogram config
    std::vector<CARTA::SetHistogramRequirements_HistogramConfig> m_histogramReqs;
    // Statistics config
    std::vector<int> m_statsReqs; // CARTA::StatsType requirements for this region

    // Cache calculations
    // MinMax: first key is stokes, second is channel number (-2 all channels for cube)
    std::unordered_map<int, std::unordered_map<int, minmax_t>> m_minmax;
    // Histogram: first key is stokes, second is channel number (-2 all channels for cube)
    std::unordered_map<int, std::unordered_map<int, CARTA::Histogram>> m_histograms;
    // Region stats: first key is stokes, second is channel number
    std::unordered_map<int, std::unordered_map<int, std::vector<double>>> m_statsData;
};

} // namespace carta
#endif // CARTA_BACKEND_REGION_REGIONSTATS_H_
