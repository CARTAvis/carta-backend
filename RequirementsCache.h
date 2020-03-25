#ifndef CARTA_BACKEND__REQUIREMENTSCACHE_H_
#define CARTA_BACKEND__REQUIREMENTSCACHE_H_

#include "ImageStats/Histogram.h"

struct HistogramConfig {
    int channel;
    int num_bins;
};

struct RegionHistogramConfig {
    int file_id;
    int region_id;
    std::vector<HistogramConfig> configs;
};

struct HistogramStats {
    // subset of BasicStats for CARTA::Histogram submessage
    double mean;
    double std_dev;
};

struct HistogramCache {
    int channel;
    int stokes;
    HistogramStats stats;
    carta::HistogramResults results;

    HistogramCache(int channel_, int stokes_, HistogramStats& stats_, carta::HistogramResults& results_) {
        channel = channel_;
        stokes = stokes_;
        stats = stats_;
        results = results_;
    }
};

struct RegionHistogramCache {
    int file_id;
    int region_id;
    std::vector<HistogramCache> histogram_cache;

    void SetHistogramCache(int file_id_, int channel_, int stokes_, HistogramStats& stats_, carta::HistogramResults& results_) {
        file_id = file_id_;
        HistogramCache cache(channel_, stokes_, stats_, results_);
        histogram_cache.push_back(cache);
    }

    bool GetHistogramCache(
        int file_id_, int channel_, int stokes_, int num_bins_, HistogramStats& stats_, carta::HistogramResults& results_) {
        if (file_id != file_id_) {
            return false;
        }

        for (auto& cache : histogram_cache) {
            if ((cache.channel == channel_) && (cache.stokes == stokes_) && (cache.results.num_bins == num_bins_)) {
                stats_ = cache.stats;
                results_ = cache.results;
                return true;
            }
        }
        return false;
    }
};

// -------------------------------

struct SpectralConfig {
    std::string coordinate;
    int stokes_index;
    std::vector<int> stats_types;

    SpectralConfig() {}
    SpectralConfig(std::string& coordinate_, int stokes_index_, std::vector<int>& stats_types_) {
        coordinate = coordinate_;
        stokes_index = stokes_index_;
        stats_types = stats_types_;
    }

    bool operator==(const SpectralConfig& rhs) const {
        return ((coordinate == rhs.coordinate) && (stokes_index == rhs.stokes_index) && (stats_types == rhs.stats_types));
    }
};

struct RegionSpectralConfig {
    int file_id;
    int region_id;
    std::vector<SpectralConfig> configs;
};

struct SpectralCache {
    int stokes;
    std::unordered_map<CARTA::StatsType, std::vector<double>> profiles;
};

struct RegionSpectralCache {
    int file_id;
    int region_id;
    std::vector<SpectralCache> profile_cache;

    bool GetProfile(int file_id_, int stokes_, CARTA::StatsType type_, std::vector<double>& profile_) {
        if (file_id != file_id) {
            return false;
        }
        for (auto& cache : profile_cache) {
            if ((cache.stokes == stokes_) && cache.profiles.count(type_)) {
                profile_ = cache.profiles.at(type_);
                return true;
            }
        }
        return false;
    }
};

// -------------------------------

struct RegionStatsConfig {
    int file_id;
    int region_id;
    std::vector<int> stats_types;
};

struct StatsCache {
    int channel;
    int stokes;
    std::map<CARTA::StatsType, double> stats;
};

struct RegionStatsCache {
    int file_id;
    int region_id;
    std::vector<StatsCache> stats;

    bool GetStats(int file_id_, int channel_, int stokes_, std::map<CARTA::StatsType, double>& stats_) {
        if (file_id != file_id) {
            return false;
        }
        for (auto& cache : stats) {
            if ((cache.channel == channel_) && (cache.stokes == stokes_)) {
                stats_ = cache.stats;
                return true;
            }
        }
        return false;
    }
};

#endif // CARTA_BACKEND__REQUIREMENTSCACHE_H_
