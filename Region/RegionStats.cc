//# RegionStats.cc: implementation of class for calculating region statistics and histograms

#include "RegionStats.h"

#include <cmath>

#include <fmt/format.h>

#include "../InterfaceConstants.h"

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

bool RegionStats::GetBasicStats(int channel, int stokes, BasicStats<float>& stats) {
    // Get stored BasicStats for given channel and stokes; return value indicates status
    bool have_basic_stats(false);
    if (_histograms_valid) {
        try {
            stats = _basic_stats.at(stokes).at(channel);
            have_basic_stats = true;
        } catch (std::out_of_range) {
            // not stored
        }
    }
    return have_basic_stats;
}

void RegionStats::SetBasicStats(int channel, int stokes, const BasicStats<float>& stats) {
    // Save BasicStats for given channel and stokes
    if (channel == ALL_CHANNELS) { // all channels (cube); don't save intermediate channel min/max
        _basic_stats[stokes].clear();
    }
    _basic_stats[stokes][channel] = stats;
}

void RegionStats::CalcRegionBasicStats(int channel, int stokes, const std::vector<float>& data, BasicStats<float>& stats) {
    // Calculate and store BasicStats
    CalcBasicStats(data, stats);
    SetBasicStats(channel, stokes, stats);
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

void RegionStats::CalcRegionHistogram(int channel, int stokes, int num_bins, const BasicStats<float>& stats, const std::vector<float>& data,
    CARTA::Histogram& histogram_msg) {
    // Calculate and store histogram for given channel, stokes, nbins; return histogram
    HistogramResults results;
    CalcHistogram(num_bins, stats, data, results);

    // fill histogram message
    histogram_msg.set_channel(channel);
    histogram_msg.set_num_bins(num_bins);
    histogram_msg.set_bin_width(results.bin_width);
    histogram_msg.set_first_bin_center(results.bin_center);
    histogram_msg.set_mean(stats.mean);
    histogram_msg.set_std_dev(stats.stdDev);
    *histogram_msg.mutable_bins() = {results.histogram_bins.begin(), results.histogram_bins.end()};

    // save message for next time
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
        std::map<CARTA::StatsType, std::vector<double>> results;
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
            bool have_stats = CalcRegionStats(results, _stats_reqs, image);
            // update message whether have stats or not
            for (size_t i = 0; i < _stats_reqs.size(); ++i) {
                // add StatisticsValue to message
                auto stats_value = stats_data.add_statistics();
                auto carta_stats_type = static_cast<CARTA::StatsType>(_stats_reqs[i]);
                stats_value->set_stats_type(carta_stats_type);
                double value(0.0);
                if (!have_stats || !results.count(carta_stats_type) || results[carta_stats_type].empty()) { // region outside image or NaNs
                    if (carta_stats_type != CARTA::StatsType::NumPixels) {
                        value = std::numeric_limits<double>::quiet_NaN();
                    }
                } else {
                    value = results[carta_stats_type][0];
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

void RegionStats::FillStatsData(CARTA::RegionStatsData& stats_data, std::map<CARTA::StatsType, double>& stats_values) {
    // Fill stats calculated externally
    for (size_t i = 0; i < _stats_reqs.size(); ++i) {
        // add StatisticsValue to message
        auto stats_value = stats_data.add_statistics();
        auto carta_stats_type = static_cast<CARTA::StatsType>(_stats_reqs[i]);
        stats_value->set_stats_type(carta_stats_type);
        double value(0.0);
        if (stats_values.find(carta_stats_type) == stats_values.end()) { // stat not provided
            if (carta_stats_type != CARTA::StatsType::NumPixels) {
                value = std::numeric_limits<double>::quiet_NaN();
            }
        } else {
            value = stats_values[carta_stats_type];
        }
        stats_value->set_value(value);
    }
}

bool RegionStats::CalcRegionStats(std::map<CARTA::StatsType, std::vector<double>>& stats_values, const std::vector<int>& requested_stats,
    const casacore::ImageInterface<float>& image) {
    // Fill stats_values vector for requested stats; one vector<float> per stat if per channel,
    // else one value per stat per region.
    if (image.shape().empty()) { // outside image or all masked (NaN)
        return false;
    }

    bool per_channel(false);
    return CalcStatsValues(stats_values, requested_stats, image, per_channel);
}
