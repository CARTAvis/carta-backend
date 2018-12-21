//# RegionStats.h: class for calculating region statistics and histograms

#pragma once

#include <carta-protobuf/defs.pb.h>  // Histogram, StatisticsValue
#include <carta-protobuf/region_requirements.pb.h>  // HistogramConfig
#include <carta-protobuf/region_stats.pb.h>  // RegionStatsData

#include <casacore/casa/Arrays/Matrix.h>
#include <casacore/casa/Arrays/IPosition.h>
#include <casacore/lattices/Lattices/SubLattice.h>

#include <vector>
#include <unordered_map>

namespace carta {

class RegionStats {

public:
    // Histograms
    bool setHistogramRequirements(const std::vector<CARTA::SetHistogramRequirements_HistogramConfig>& histogramReqs);
    size_t numHistogramConfigs();
    CARTA::SetHistogramRequirements_HistogramConfig getHistogramConfig(int histogramIndex);
    void getMinMax(float& minVal, float& maxVal, const std::vector<float>& data);
    void fillHistogram(CARTA::Histogram* histogram, const std::vector<float>& data,
        const size_t chanIndex, const size_t stokesIndex, const int nBins, const float minVal, const float maxVal);
    bool getChannelHistogram(CARTA::Histogram* histogram, int channel, int stokes, int numBins);

    // Stats
    void setStatsRequirements(const std::vector<int>& statsTypes);
    size_t numStats();
    void fillStatsData(CARTA::RegionStatsData& statsData, const casacore::SubLattice<float>& subLattice);
    bool getStatsValues(std::vector<std::vector<float>>& statsValues,
        const std::vector<int>& requestedStats, const casacore::SubLattice<float>& lattice);

private:
    // Histograms
    std::vector<CARTA::SetHistogramRequirements_HistogramConfig> m_configs;
    // cache latest histogram only
    size_t m_channel, m_stokes, m_bins;
    CARTA::Histogram* m_channelHistogram;

    // Statistics
    std::vector<int> m_regionStats; // CARTA::StatsType requirements
};

}
