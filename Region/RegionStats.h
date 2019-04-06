//# RegionStats.h: class for calculating region statistics and histograms

#pragma once

#include <carta-protobuf/defs.pb.h>  // Histogram, StatisticsValue
#include <carta-protobuf/region_requirements.pb.h>  // HistogramConfig
#include <carta-protobuf/region_stats.pb.h>  // RegionStatsData

#include <casacore/lattices/Lattices/SubLattice.h>

#include <vector>
#include <unordered_map>

namespace carta {

class RegionStats {


public:
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
    void calcHistogram(int channel, int stokes, int nBins, float minVal, float maxVal,
        const std::vector<float>& data, CARTA::Histogram& histogramMsg);

    // Stats
    void setStatsRequirements(const std::vector<int>& statsTypes);
    size_t numStats();
    void fillStatsData(CARTA::RegionStatsData& statsData, const casacore::SubLattice<float>& subLattice);
    bool getStatsValues(std::vector<std::vector<double>>& statsValues,
        const std::vector<int>& requestedStats, const casacore::SubLattice<float>& lattice,
        bool perChannel=true);

    // invalidate stored calculations (only histograms for now) for new region
    inline void clearStats() { m_histogramsValid = false; };

private:

    bool m_histogramsValid; // for current region

    // Histogram config
    std::vector<CARTA::SetHistogramRequirements_HistogramConfig> m_configs;

    // Statistics config
    std::vector<int> m_regionStats; // CARTA::StatsType requirements for this region

    // MinMax, histogram maps to store calculations
    // first key is stokes, second is channel number (-2 all channels for cube)
    std::unordered_map<int, std::unordered_map<int, minmax_t>> m_minmax;
    std::unordered_map<int, std::unordered_map<int, CARTA::Histogram>> m_histograms;

};

}
