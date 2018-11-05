//# RegionStats.h: class for calculating region statistics and histograms

#pragma once

#include <carta-protobuf/region_requirements.pb.h>  // HistogramConfig
#include <carta-protobuf/defs.pb.h>  // Histogram, StatisticsValue
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
    void fillHistogram(CARTA::Histogram* histogram, const casacore::Matrix<float>& chanMatrix,
        const size_t chanIndex, const size_t stokesIndex);

    // Stats
    void setStatsRequirements(const std::vector<int>& statsTypes);
    void fillStatsData(CARTA::RegionStatsData& statsData, const casacore::SubLattice<float>& subLattice);
    bool getStatsValues(std::vector<std::vector<float>>& statsValues,
        const std::vector<int>& requestedStats, const casacore::SubLattice<float>& lattice);

private:
    // Histograms
    size_t m_stokes;
    std::unordered_map<int, CARTA::Histogram> m_channelHistograms;
    std::vector<CARTA::SetHistogramRequirements_HistogramConfig> m_configs;

    // Statistics
    std::vector<int> m_regionStats; // CARTA::StatsType requirements
};

}
