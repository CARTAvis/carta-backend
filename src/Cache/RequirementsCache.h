/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__REQUIREMENTSCACHE_H_
#define CARTA_BACKEND__REQUIREMENTSCACHE_H_

#include "ImageStats/BasicStatsCalculator.h"
#include "ImageStats/Histogram.h"
#include "Util/Message.h"

namespace carta {

struct ConfigId {
    int file_id;
    int region_id;

    ConfigId() {}
    ConfigId(int file, int region) : file_id(file), region_id(region) {}

    bool operator==(const ConfigId& rhs) const {
        return (file_id == rhs.file_id) && (region_id == rhs.region_id);
    }
};

struct ConfigIdHash {
    std::size_t operator()(ConfigId const& id) const noexcept {
        std::size_t h1 = std::hash<int>{}(id.file_id);
        std::size_t h2 = std::hash<int>{}(id.region_id);
        return h1 ^ (h2 << 1);
    }
};

// -------------------------------

struct CacheId {
    int file_id;
    int region_id;
    int stokes;
    int channel;

    CacheId() {}
    CacheId(int file, int region, int stokes, int channel = -1) : file_id(file), region_id(region), stokes(stokes), channel(channel) {}

    bool operator==(const CacheId& rhs) const {
        return (file_id == rhs.file_id) && (region_id == rhs.region_id) && (stokes == rhs.stokes) && (channel == rhs.channel);
    }
};

struct CacheIdHash {
    std::size_t operator()(CacheId const& id) const noexcept {
        std::size_t h1 = std::hash<int>{}(id.file_id);
        std::size_t h2 = std::hash<int>{}(id.region_id);
        std::size_t h3 = std::hash<int>{}(id.stokes);
        std::size_t h4 = std::hash<int>{}(id.channel);
        return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3);
    }
};

// -------------------------------

struct HistogramConfig {
    std::string coordinate;
    int channel;
    bool fixed_num_bins;
    int num_bins;
    bool fixed_bounds;
    float min_val;
    float max_val;

    HistogramConfig()
        : coordinate("z"),
          channel(CURRENT_Z),
          fixed_num_bins(false),
          num_bins(AUTO_BIN_SIZE),
          fixed_bounds(false),
          min_val(0),
          max_val(0) {}

    HistogramConfig(const CARTA::HistogramConfig& config)
        : coordinate(config.coordinate()),
          channel(config.channel()),
          fixed_num_bins(config.fixed_num_bins()),
          num_bins(config.num_bins()),
          fixed_bounds(config.fixed_bounds()),
          min_val(config.bounds().min()),
          max_val(config.bounds().max()) {}

    bool operator!=(const HistogramConfig& rhs) const {
        return (coordinate != rhs.coordinate) || (channel != rhs.channel) || (fixed_num_bins != rhs.fixed_num_bins) ||
               (fixed_bounds != rhs.fixed_bounds) || (num_bins != rhs.num_bins) || !AreEqual(min_val, rhs.min_val) ||
               !AreEqual(max_val, rhs.max_val);
    }
};

struct RegionHistogramConfig {
    std::vector<HistogramConfig> configs;
};

struct HistogramCache {
    BasicStats<float> stats;                       // Statistics data without fixed bounds
    std::unordered_map<int, Histogram> histograms; // key is num_bins

    HistogramCache() {}

    bool GetBasicStats(BasicStats<float>& stats_) {
        if (stats.num_pixels > 0) {
            stats_ = stats;
            return true;
        }
        return false;
    }

    void SetBasicStats(BasicStats<float>& stats_) {
        stats = stats_;
    }

    bool GetHistogram(int num_bins_, float min_val, float max_val, Histogram& histogram_) {
        if (histograms.count(num_bins_)) {
            const auto& hist = histograms.at(num_bins_);
            if (AreEqual(hist.GetMinVal(), min_val) && AreEqual(hist.GetMaxVal(), max_val)) {
                histogram_ = hist;
                return true;
            }
        }
        return false;
    }

    void SetHistogram(int num_bins_, Histogram& histogram_) {
        histograms[num_bins_] = histogram_;
    }

    void ClearHistograms() {
        stats = BasicStats<float>();
        histograms.clear();
    }
};

// -------------------------------

struct SpectralConfig {
    std::string coordinate;
    std::vector<CARTA::StatsType> all_stats;
    std::vector<CARTA::StatsType> new_stats;

    SpectralConfig(std::string& coordinate, std::vector<CARTA::StatsType>& stats)
        : coordinate(coordinate), all_stats(stats), new_stats(stats) {}

    void SetNewRequirements(const std::vector<CARTA::StatsType>& new_stats_types) {
        new_stats = new_stats_types;
    }

    void SetAllNewStats() {
        // When region changes, all stats must be sent
        new_stats = all_stats;
    }

    void ClearNewStats() {
        // When all stats sent, clear list
        new_stats.clear();
    }

    bool HasStat(CARTA::StatsType type) {
        // Cancel when stat no longer in requirements
        for (const auto& stat : all_stats) {
            if (stat == type) {
                return true;
            }
        }
        return false;
    }
};

struct RegionSpectralConfig {
    std::vector<SpectralConfig> configs;
};

struct SpectralCache {
    std::map<CARTA::StatsType, std::vector<double>> profiles;

    SpectralCache() {}
    SpectralCache(std::map<CARTA::StatsType, std::vector<double>>& profiles_) : profiles(profiles_) {}

    bool GetProfile(CARTA::StatsType type_, std::vector<double>& profile_) {
        if (!profiles.empty() && profiles.count(type_)) {
            profile_ = profiles.at(type_);
            return true;
        }
        return false;
    }

    void ClearProfiles() {
        // when region changes
        profiles.clear();
    }
};

// -------------------------------

struct RegionStatsConfig {
    std::vector<CARTA::SetStatsRequirements_StatsConfig> stats_configs;
};

struct StatsCache {
    std::map<CARTA::StatsType, double> stats;

    StatsCache() {}
    StatsCache(std::map<CARTA::StatsType, double>& stats_) {
        stats = stats_;
    }

    bool GetStats(std::map<CARTA::StatsType, double>& stats_) {
        if (!stats.empty()) {
            stats_ = stats;
            return true;
        }
        return false;
    }

    void ClearStats() {
        // When region changes
        stats.clear();
    }
};

} // namespace carta

#endif // CARTA_BACKEND__REQUIREMENTSCACHE_H_
