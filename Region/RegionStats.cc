//# RegionStats.cc: implementation of class for calculating region statistics and histograms

#include "RegionStats.h"

#include <cmath>
#include <limits>

#include <fmt/format.h>
#include <tbb/blocked_range.h>
#include <tbb/parallel_reduce.h>

#include <casacore/casa/Arrays/ArrayMath.h>
#include <casacore/lattices/LatticeMath/LatticeStatistics.h>

#include "../InterfaceConstants.h"
#include "Histogram.h"
#include "MinMax.h"

using namespace carta;
using namespace std;

RegionStats::RegionStats() {
    ClearStats();
}

// ***** Cache *****

void RegionStats::ClearStats() {
    _histograms_valid = false;
    _stats_valid = false;
}

// ***** Histograms *****

// config
bool RegionStats::SetHistogramRequirements(const std::vector<CARTA::SetHistogramRequirements_HistogramConfig>& histogram_reqs) {
    _histogram_reqs = histogram_reqs;
    return true;
}

size_t RegionStats::NumHistogramConfigs() {
    return _histogram_reqs.size();
}

CARTA::SetHistogramRequirements_HistogramConfig RegionStats::GetHistogramConfig(int histogram_index) {
    CARTA::SetHistogramRequirements_HistogramConfig config;
    if (histogram_index < _histogram_reqs.size())
        config = _histogram_reqs[histogram_index];
    return config;
}

// min max
bool RegionStats::GetMinMax(int channel, int stokes, float& min_val, float& max_val) {
    // Get stored min,max for given channel and stokes; return value indicates status
    bool have_min_max(false);
    if (_histograms_valid) {
        try {
            minmax_t vals = _minmax.at(stokes).at(channel);
            min_val = vals.first;
            max_val = vals.second;
            have_min_max = true;
        } catch (std::out_of_range) {
            // not stored
        }
    }
    return have_min_max;
}

void RegionStats::SetMinMax(int channel, int stokes, minmax_t minmax_vals) {
    // Save min, max for given channel and stokes
    if (channel == ALL_CHANNELS) { // all channels (cube); don't save intermediate channel min/max
        _minmax[stokes].clear();
    }
    _minmax[stokes][channel] = minmax_vals;
}

void RegionStats::CalcMinMax(int channel, int stokes, const std::vector<float>& data, float& min_val, float& max_val) {
    // Calculate and store min, max values in data; return min_val and maxval
    tbb::blocked_range<size_t> range(0, data.size());
    MinMax<float> mm(data);
    tbb::parallel_reduce(range, mm);
    minmax_t minmax_vals(mm.getMinMax());
    SetMinMax(channel, stokes, minmax_vals);
    min_val = minmax_vals.first;
    max_val = minmax_vals.second;
}

bool RegionStats::GetHistogram(int channel, int stokes, int num_bins, CARTA::Histogram& histogram) {
    // Get stored histogram for given channel and stokes; return value indicates status
    bool have_histogram(false);
    if (_histograms_valid) {
        try {
            CARTA::Histogram stored_histogram = _histograms.at(stokes).at(channel);
            if (stored_histogram.num_bins() == num_bins) {
                histogram = stored_histogram;
                have_histogram = true;
            }
        } catch (std::out_of_range) {
            // not stored
        }
    }
    return have_histogram;
}

void RegionStats::SetHistogram(int channel, int stokes, CARTA::Histogram& histogram) {
    // Store histogram for given channel and stokes
    if (!_histograms_valid)
        _histograms.clear();
    if (channel == ALL_CHANNELS) { // all channels(cube); don't save intermediate channel histograms
        _histograms[stokes].clear();
    }
    _histograms[stokes][channel] = histogram;
    _histograms_valid = true;
}

void RegionStats::CalcHistogram(
    int channel, int stokes, int num_bins, float min_val, float max_val, const std::vector<float>& data, CARTA::Histogram& histogram_msg) {
    // Calculate and store histogram for given channel, stokes, nbins; return histogram
    float bin_width(0), bin_center(0);
    std::vector<int> histogram_bins;
    if ((min_val == std::numeric_limits<float>::max()) || (max_val == std::numeric_limits<float>::min()) || data.empty()) {
        // empty / NaN region
        histogram_bins.resize(num_bins, 0);
    } else {
        tbb::blocked_range<size_t> range(0, data.size());
        Histogram hist(num_bins, min_val, max_val, data);
        tbb::parallel_reduce(range, hist);
        histogram_bins = hist.getHistogram();
        bin_width = hist.getBinWidth();
        bin_center = min_val + (bin_width / 2.0);
    }

    // fill histogram message
    histogram_msg.set_channel(channel);
    histogram_msg.set_num_bins(num_bins);
    histogram_msg.set_bin_width(bin_width);
    histogram_msg.set_first_bin_center(bin_center);
    *histogram_msg.mutable_bins() = {histogram_bins.begin(), histogram_bins.end()};

    // save for next time
    SetHistogram(channel, stokes, histogram_msg);
}

// ***** Statistics *****

void RegionStats::SetStatsRequirements(const std::vector<int>& stats_types) {
    _stats_reqs = stats_types;
}

size_t RegionStats::NumStats() {
    return _stats_reqs.size();
}

void RegionStats::FillStatsData(CARTA::RegionStatsData& stats_data, const casacore::ImageInterface<float>& image, int channel, int stokes) {
    // Fill RegionStatsData with statistics types set in requirements.
    if (_stats_reqs.empty()) { // no requirements set, add empty StatisticsValue
        auto stats_value = stats_data.add_statistics();
        stats_value->set_stats_type(CARTA::StatsType::Sum);
    } else {
        std::vector<std::vector<double>> results;
        if (_stats_valid && (_stats_data.count(stokes)) && (_stats_data.at(stokes).count(channel))) {
            // used stored stats
            try {
                std::vector<double> stored_stats(_stats_data.at(stokes).at(channel));
                for (size_t i = 0; i < _stats_reqs.size(); ++i) {
                    // add StatisticsValue to message
                    auto stats_value = stats_data.add_statistics();
                    auto carta_stats_type = static_cast<CARTA::StatsType>(_stats_reqs[i]);
                    stats_value->set_stats_type(carta_stats_type);
                    stats_value->set_value(stored_stats[carta_stats_type]);
                }
            } catch (std::out_of_range& range_error) {
                // stats cleared
                auto stats_value = stats_data.add_statistics();
                stats_value->set_stats_type(CARTA::StatsType::Sum);
            }
        } else {
            // calculate stats
            if (!_stats_valid)
                _stats_data.clear();
            // per channel = false, stats for entire region
            bool have_stats(CalcStatsValues(results, _stats_reqs, image, false));
            // update message whether have stats or not
            for (size_t i = 0; i < _stats_reqs.size(); ++i) {
                // add StatisticsValue to message
                auto stats_value = stats_data.add_statistics();
                auto carta_stats_type = static_cast<CARTA::StatsType>(_stats_reqs[i]);
                stats_value->set_stats_type(carta_stats_type);
                double value(0.0);
                if (!have_stats || results[i].empty()) { // region outside image or NaNs
                    if (carta_stats_type != CARTA::NumPixels) {
                        value = std::numeric_limits<double>::quiet_NaN();
                    }
                } else {
                    value = results[i][0];
                }
                stats_value->set_value(value);

                // cache stats values
                if (_stats_data[stokes][channel].empty()) { // resize vector, set to NaN
                    _stats_data[stokes][channel].resize(CARTA::StatsType_MAX, std::numeric_limits<double>::quiet_NaN());
                }
                _stats_data[stokes][channel][carta_stats_type] = value;
            }
            _stats_valid = true;
        }
    }
}

bool RegionStats::CalcStatsValues(std::vector<std::vector<double>>& stats_values, const std::vector<int>& requested_stats,
    const casacore::ImageInterface<float>& image, bool per_channel) {
    // Fill stats_values vector for requested stats; one vector<float> per stat if per channel,
    // else one value per stat per region.
    if (image.shape().empty()) // outside image or all masked (NaN)
        return false;

    // Use ImageStatistics to fill statistics values according to type;
    // template type matches image type
    casacore::ImageStatistics<float> image_stats = casacore::ImageStatistics<float>(image,
        /*showProgress*/ false, /*forceDisk*/ false, /*clone*/ false);

    if (per_channel) { // get stats per xy plane
        casacore::Vector<int> display_axes(2);
        display_axes(0) = 0;
        display_axes(1) = 1;
        if (!image_stats.setAxes(display_axes))
            return false;
    }

    casacore::Array<casacore::Double> num_points;
    size_t num_stats(requested_stats.size());
    stats_values.resize(num_stats);
    for (size_t i = 0; i < num_stats; ++i) {
        // get requested statistics values
        casacore::LatticeStatsBase::StatisticsTypes lattice_stats_type(casacore::LatticeStatsBase::NSTATS);
        auto carta_stats_type = static_cast<CARTA::StatsType>(requested_stats[i]);

        std::vector<double> dbl_result; // lattice stats
        std::vector<int> int_result;    // position stats
        switch (carta_stats_type) {
            case CARTA::StatsType::NumPixels:
                lattice_stats_type = casacore::LatticeStatsBase::NPTS;
                break;
            case CARTA::StatsType::Sum:
                lattice_stats_type = casacore::LatticeStatsBase::SUM;
                break;
            case CARTA::StatsType::FluxDensity:
                lattice_stats_type = casacore::LatticeStatsBase::FLUX;
                break;
            case CARTA::StatsType::Mean:
                lattice_stats_type = casacore::LatticeStatsBase::MEAN;
                break;
            case CARTA::StatsType::RMS:
                lattice_stats_type = casacore::LatticeStatsBase::RMS;
                break;
            case CARTA::StatsType::Sigma:
                lattice_stats_type = casacore::LatticeStatsBase::SIGMA;
                break;
            case CARTA::StatsType::SumSq:
                lattice_stats_type = casacore::LatticeStatsBase::SUMSQ;
                break;
            case CARTA::StatsType::Min:
                lattice_stats_type = casacore::LatticeStatsBase::MIN;
                break;
            case CARTA::StatsType::Max:
                lattice_stats_type = casacore::LatticeStatsBase::MAX;
                break;
            case CARTA::StatsType::Blc: {
                const casacore::IPosition blc(image.region().slicer().start());
                int_result = blc.asStdVector();
                break;
            }
            case CARTA::StatsType::Trc: {
                const casacore::IPosition trc(image.region().slicer().end());
                int_result = trc.asStdVector();
                break;
            }
            case CARTA::StatsType::MinPos:
            case CARTA::StatsType::MaxPos: {
                if (!per_channel) { // only works when no display axes
                    const casacore::IPosition blc(image.region().slicer().start());
                    casacore::IPosition min_pos, max_pos;
                    image_stats.getMinMaxPos(min_pos, max_pos);
                    if (carta_stats_type == CARTA::StatsType::MinPos)
                        int_result = (blc + min_pos).asStdVector();
                    else // MaxPos
                        int_result = (blc + max_pos).asStdVector();
                }
                break;
            }
            default:
                break;
        }
        if (lattice_stats_type < casacore::LatticeStatsBase::NSTATS) { // get lattice statistic
            casacore::Array<casacore::Double> result;                // must be double
            if (image_stats.getStatistic(result, lattice_stats_type)) {
                if (anyEQ(result, 0.0)) { // actually 0, or NaN?
                    // NaN if number of points is zero
                    if (num_points.empty())
                        image_stats.getStatistic(num_points, casacore::LatticeStatsBase::NPTS);
                    for (size_t j = 0; j < result.size(); ++j) {
                        casacore::IPosition index(1, j);
                        if ((result(index) == 0.0) && (num_points(index) == 0.0)) {
                            result(index) = std::numeric_limits<double>::quiet_NaN();
                        }
                    }
                }
                result.tovector(dbl_result);
            }
        }

        if (!int_result.empty()) {
            dbl_result.reserve(int_result.size());
            for (unsigned int j = 0; j < int_result.size(); ++j) { // convert to double
                dbl_result.push_back(static_cast<double>(int_result[j]));
            }
        }

        if (!dbl_result.empty()) {
            stats_values[i] = std::move(dbl_result);
        }
    }
    return true;
}
