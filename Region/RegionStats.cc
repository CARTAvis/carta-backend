//# RegionStats.cc: implementation of class for calculating region statistics and histograms

#include "RegionStats.h"
#include "../InterfaceConstants.h"
#include "Histogram.h"
#include "MinMax.h"

#include <chrono>
#include <cmath>
#include <fmt/format.h>
#include <limits>
#include <tbb/blocked_range.h>
#include <tbb/blocked_range2d.h>
#include <tbb/parallel_reduce.h>

#include <casacore/casa/Arrays/ArrayMath.h>
#include <casacore/lattices/LatticeMath/LatticeStatistics.h>

using namespace carta;
using namespace std;

// ***** Histograms *****

// config
bool RegionStats::setHistogramRequirements(const std::vector<CARTA::SetHistogramRequirements_HistogramConfig>& histogramReqs) {
    m_configs = histogramReqs;
    return true;
}

size_t RegionStats::numHistogramConfigs() {
    return m_configs.size();
}

CARTA::SetHistogramRequirements_HistogramConfig RegionStats::getHistogramConfig(int histogramIndex) {
    CARTA::SetHistogramRequirements_HistogramConfig config;
    if (histogramIndex < m_configs.size())
        config = m_configs[histogramIndex];
    return config;
}

// min max
bool RegionStats::getMinMax(int channel, int stokes, float& minVal, float& maxVal) {
    // Get stored min,max for given channel and stokes; return value indicates status
    bool haveMinMax(false);
    try {
        minmax_t vals = m_minmax.at(stokes).at(channel);
        minVal = vals.first;
        maxVal = vals.second;
        haveMinMax = true;
    } catch (std::out_of_range) {
        // not stored
    }
    return haveMinMax;
}

void RegionStats::setMinMax(int channel, int stokes, minmax_t minmaxVals) {
    // Save min, max for given channel and stokes
    if (channel == ALL_CHANNELS) { // all channels (cube); don't save intermediate channel min/max
        m_minmax[stokes].clear();
    }
    m_minmax[stokes][channel] = minmaxVals;
}

void RegionStats::calcMinMax(int channel, int stokes, const std::vector<float>& data, float& minval, float& maxval) {
    // Calculate and store min, max values in data; return minval and maxval
    tbb::blocked_range<size_t> range(0, data.size());
    MinMax<float> mm(data);
    tbb::parallel_reduce(range, mm);
    minmax_t minmaxVals(mm.getMinMax());
    setMinMax(channel, stokes, minmaxVals);
    minval = minmaxVals.first;
    maxval = minmaxVals.second;
}

bool RegionStats::getHistogram(int channel, int stokes, int nbins, CARTA::Histogram& histogram) {
    // Get stored histogram for given channel and stokes; return value indicates status
    bool haveHistogram(false);
    try {
        CARTA::Histogram storedHistogram = m_histograms.at(stokes).at(channel);
        if (storedHistogram.num_bins() == nbins) {
            histogram = storedHistogram;
            haveHistogram = true;
        }
    } catch (std::out_of_range) {
        // not stored
    }
    return haveHistogram;
}

void RegionStats::setHistogram(int channel, int stokes, CARTA::Histogram& histogram) {
    // Store histogram for given channel and stokes
    if (channel == ALL_CHANNELS) { // all channels(cube); don't save intermediate channel histograms
        m_histograms[stokes].clear();
    }
    m_histograms[stokes][channel] = histogram;
}

void RegionStats::calcHistogram(int channel, int stokes, int nBins, float minVal, float maxVal,
        const std::vector<float>& data, CARTA::Histogram& histogramMsg) {
    // Calculate and store histogram for given channel, stokes, nbins; return histogram
    tbb::blocked_range<size_t> range(0, data.size());
    Histogram hist(nBins, minVal, maxVal, data);
    tbb::parallel_reduce(range, hist);
    std::vector<int> histogramBins(hist.getHistogram());
    float binWidth(hist.getBinWidth());

    // fill histogram message
    histogramMsg.set_channel(channel);
    histogramMsg.set_num_bins(nBins);
    histogramMsg.set_bin_width(binWidth);
    histogramMsg.set_first_bin_center(minVal + (binWidth / 2.0));
    *histogramMsg.mutable_bins() = {histogramBins.begin(), histogramBins.end()};

    // save for next time
    setHistogram(channel, stokes, histogramMsg);
}

// ***** Statistics *****

void RegionStats::setStatsRequirements(const std::vector<int>& statsTypes) {
    m_regionStats = statsTypes;
}

size_t RegionStats::numStats() {
   return m_regionStats.size();
}

void RegionStats::fillStatsData(CARTA::RegionStatsData& statsData, const casacore::SubLattice<float>& subLattice) {
    // fill RegionStatsData with statistics types set in requirements

    if (m_regionStats.empty()) {  // no requirements set
        // add empty StatisticsValue
        auto statsValue = statsData.add_statistics();
        statsValue->set_stats_type(CARTA::StatsType::None);
        return;
    }

    std::vector<std::vector<double>> results;
    if (getStatsValues(results, m_regionStats, subLattice, false)) { // entire region, not per channel
        for (size_t i=0; i<m_regionStats.size(); ++i) {
            auto statType = static_cast<CARTA::StatsType>(m_regionStats[i]);
            // add StatisticsValue
            auto statsValue = statsData.add_statistics();
            statsValue->set_stats_type(statType);
            if (!results[i].empty()) {
                statsValue->set_value(results[i][0]);
            } else {
                statsValue->set_value(std::numeric_limits<float>::quiet_NaN());
            }
        }
    }
}

bool RegionStats::getStatsValues(std::vector<std::vector<double>>& statsValues,
    const std::vector<int>& requestedStats, const casacore::SubLattice<float>& subLattice,
    bool perChannel) {
    // Fill statsValues vector for requested stats; one vector<float> per stat if per channel,
    // else one value per stat per region.

    // Use LatticeStatistics to fill statistics values according to type;
    // template type matches sublattice type
    casacore::LatticeStatistics<float> latticeStats = casacore::LatticeStatistics<float>(subLattice,
            /*showProgress*/ false, /*forceDisk*/ false, /*clone*/ false);

    if (perChannel) { // get stats per xy plane
        casacore::Vector<int> displayAxes(2);
        displayAxes(0) = 0;
        displayAxes(1) = 1;
        if (!latticeStats.setAxes(displayAxes))
            return false;
    }

    // use LatticeRegion for positional stats
    const casacore::LatticeRegion* lregion = subLattice.getRegionPtr();
    casacore::Slicer lrSlicer = lregion->slicer();

    // Print region info
    casacore::IPosition blc(lrSlicer.start()), trc(lrSlicer.end());
    casacore::Array<casacore::Double> npts; 

    size_t nstats(requestedStats.size());
    statsValues.resize(nstats);
    for (size_t i=0; i<nstats; ++i) {
        // get requested statistics values
        casacore::LatticeStatsBase::StatisticsTypes lattStatsType(casacore::LatticeStatsBase::NSTATS);
        auto statType = static_cast<CARTA::StatsType>(requestedStats[i]);

        std::vector<double> dblResult; // lattice stats
        std::vector<int> intResult;    // position stats
        switch (statType) {
            case CARTA::StatsType::None:
                break;
            case CARTA::StatsType::Sum:
                lattStatsType = casacore::LatticeStatsBase::SUM;
                break;
            case CARTA::StatsType::FluxDensity:  // not supported for lattice statistics
                //lattStatsType = casacore::LatticeStatsBase::FLUX;
                break;
            case CARTA::StatsType::Mean:
                lattStatsType = casacore::LatticeStatsBase::MEAN;
                break;
            case CARTA::StatsType::RMS:
                lattStatsType = casacore::LatticeStatsBase::RMS;
                break;
            case CARTA::StatsType::Sigma:
                lattStatsType = casacore::LatticeStatsBase::SIGMA;
                break;
            case CARTA::StatsType::SumSq:
                lattStatsType = casacore::LatticeStatsBase::SUMSQ;
                break;
            case CARTA::StatsType::Min:
                lattStatsType = casacore::LatticeStatsBase::MIN;
                break;
            case CARTA::StatsType::Max:
                lattStatsType = casacore::LatticeStatsBase::MAX;
                break;
            case CARTA::StatsType::Blc:
                intResult = blc.asStdVector();
                break;
            case CARTA::StatsType::Trc:
                intResult = trc.asStdVector();
                break;
            case CARTA::StatsType::MinPos:
            case CARTA::StatsType::MaxPos: {
                if (!perChannel) { // only works when no display axes
                    casacore::IPosition minPos, maxPos;
                    latticeStats.getMinMaxPos(minPos, maxPos);
                    if (statType==CARTA::StatsType::MinPos)
                        intResult = (blc + minPos).asStdVector();
                    else // MaxPos
                        intResult = (blc + maxPos).asStdVector();
                    }
                }
                break;
            default:
                break;
        }
        if (lattStatsType < casacore::LatticeStatsBase::NSTATS) { // get lattice statistic
            casacore::Array<casacore::Double> result; // must be double
            if (latticeStats.getStatistic(result, lattStatsType)) {
                result.tovector(dblResult);
            }
        }

        if (!intResult.empty()) {
            dblResult.reserve(intResult.size());
            for (unsigned int i=0; i<intResult.size(); ++i) {  // convert to double
                dblResult.push_back(static_cast<double>(intResult[i]));
            }
        }

        if (!dblResult.empty()) {
            statsValues[i] = std::move(dblResult);
        }
    }
    return true;
}

