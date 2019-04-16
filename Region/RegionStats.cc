//# RegionStats.cc: implementation of class for calculating region statistics and histograms

#include "RegionStats.h"
#include "../InterfaceConstants.h"
#include "Histogram.h"
#include "MinMax.h"

#include <cmath>
#include <limits>
#include <fmt/format.h>
#include <tbb/blocked_range.h>
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
    if (m_histogramsValid) {
        try {
            minmax_t vals = m_minmax.at(stokes).at(channel);
            minVal = vals.first;
            maxVal = vals.second;
            haveMinMax = true;
        } catch (std::out_of_range) {
            // not stored
        }
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
    if (m_histogramsValid) {
        try {
            CARTA::Histogram storedHistogram = m_histograms.at(stokes).at(channel);
            if (storedHistogram.num_bins() == nbins) {
                histogram = storedHistogram;
                haveHistogram = true;
            }
        } catch (std::out_of_range) {
            // not stored
        }
    }
    return haveHistogram;
}

void RegionStats::setHistogram(int channel, int stokes, CARTA::Histogram& histogram) {
    // Store histogram for given channel and stokes
    if (channel == ALL_CHANNELS) { // all channels(cube); don't save intermediate channel histograms
        m_histograms[stokes].clear();
    }
    m_histograms[stokes][channel] = histogram;
    m_histogramsValid = true;
}

void RegionStats::calcHistogram(int channel, int stokes, int nBins, float minVal, float maxVal,
        const std::vector<float>& data, CARTA::Histogram& histogramMsg) {
    // Calculate and store histogram for given channel, stokes, nbins; return histogram
    float binWidth(0), binCenter(0);
    std::vector<int> histogramBins;
    if ((minVal == std::numeric_limits<float>::max()) || (maxVal == std::numeric_limits<float>::min())
         || data.empty()) {
        // empty / NaN region
        histogramBins.resize(nBins, 0);
    } else {
        tbb::blocked_range<size_t> range(0, data.size());
        Histogram hist(nBins, minVal, maxVal, data);
        tbb::parallel_reduce(range, hist);
        histogramBins = hist.getHistogram();
        binWidth = hist.getBinWidth();
        binCenter = minVal + (binWidth / 2.0);
    }

    // fill histogram message
    histogramMsg.set_channel(channel);
    histogramMsg.set_num_bins(nBins);
    histogramMsg.set_bin_width(binWidth);
    histogramMsg.set_first_bin_center(binCenter);
    *histogramMsg.mutable_bins() = {histogramBins.begin(), histogramBins.end()};

    // save for next time
    setHistogram(channel, stokes, histogramMsg);
    m_histogramsValid = true;
}

// ***** Statistics *****

void RegionStats::setStatsRequirements(const std::vector<int>& statsTypes) {
    m_regionStats = statsTypes;
}

size_t RegionStats::numStats() {
   return m_regionStats.size();
}

void RegionStats::fillStatsData(CARTA::RegionStatsData& statsData, const casacore::SubLattice<float>& subLattice) {
    // Fill RegionStatsData with statistics types set in requirements.
    // Sublattice shape may be empty because of no xyregion (outside image or 0 pixels selected),
    // or lattice mask removed all NaN values
    if (m_regionStats.empty()) {  // no requirements set, add empty StatisticsValue
        auto statsValue = statsData.add_statistics();
        statsValue->set_stats_type(CARTA::StatsType::None);
        return;
    } else {
        std::vector<std::vector<double>> results;
        // stats for entire region, not per channel
        bool haveStats(getStatsValues(results, m_regionStats, subLattice, false));
        // update message whether have stats or not
        for (size_t i=0; i<m_regionStats.size(); ++i) {
            // add StatisticsValue to message
            auto statsValue = statsData.add_statistics();
            auto statType = static_cast<CARTA::StatsType>(m_regionStats[i]);
            statsValue->set_stats_type(statType);
            if (!haveStats || results[i].empty()) { // region outside image or NaNs
                if (statType==CARTA::NumPixels) {
                    statsValue->set_value(0.0);
                } else {
                    statsValue->set_value(std::numeric_limits<double>::quiet_NaN());
                }
            } else {
                statsValue->set_value(results[i][0]);
            }
        }
    }
}

bool RegionStats::getStatsValues(std::vector<std::vector<double>>& statsValues,
    const std::vector<int>& requestedStats, const casacore::SubLattice<float>& subLattice,
    bool perChannel) {
    // Fill statsValues vector for requested stats; one vector<float> per stat if per channel,
    // else one value per stat per region.
    if (subLattice.shape().empty()) // outside image or all masked (NaN)
        return false;

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
            case CARTA::StatsType::NumPixels:
                lattStatsType = casacore::LatticeStatsBase::NPTS;
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
            case CARTA::StatsType::Blc: {
                const casacore::IPosition blc(subLattice.getRegionPtr()->slicer().start());
                intResult = blc.asStdVector();
                break;
            }
            case CARTA::StatsType::Trc: {
                const casacore::IPosition trc(subLattice.getRegionPtr()->slicer().end());
                intResult = trc.asStdVector();
                break;
            }
            case CARTA::StatsType::MinPos:
            case CARTA::StatsType::MaxPos: {
                if (!perChannel) { // only works when no display axes
                    const casacore::IPosition blc(subLattice.getRegionPtr()->slicer().start());
                    casacore::IPosition minPos, maxPos;
                    latticeStats.getMinMaxPos(minPos, maxPos);
                    if (statType==CARTA::StatsType::MinPos)
                        intResult = (blc + minPos).asStdVector();
                    else // MaxPos
                        intResult = (blc + maxPos).asStdVector();
                }
                break;
            }
            default:
                break;
        }
        if (lattStatsType < casacore::LatticeStatsBase::NSTATS) { // get lattice statistic
            casacore::Array<casacore::Double> result; // must be double
            if (latticeStats.getStatistic(result, lattStatsType)) {
                if (anyEQ(result, 0.0)) { // actually 0, or NaN?
                    // NaN if number of points is zero
                    if (npts.empty())
                        latticeStats.getStatistic(npts, casacore::LatticeStatsBase::NPTS);
                    for (size_t i=0; i<result.size(); ++i) {
                        casacore::IPosition index(1,i);
                        if ((result(index) == 0.0) && (npts(index) == 0.0)) {
                            result(index) = std::numeric_limits<double>::quiet_NaN();
                        }
                    }
                }
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

