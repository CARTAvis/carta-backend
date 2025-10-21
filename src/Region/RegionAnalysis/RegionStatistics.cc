/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// RegionStatistics.cc: Calculate statistics for a region in a frame

#include "RegionStatistics.h"

#include "Util/File.h"
#include "Util/Message.h"

using namespace carta;

RegionStatistics::RegionStatistics(int region_id, int file_id, const std::vector<CARTA::SetStatsRequirements_StatsConfig>& configs)
    : _region_id(region_id) {
    SetConfigurations(file_id, configs);
}

void RegionStatistics::SetConfigurations(int file_id, const std::vector<CARTA::SetStatsRequirements_StatsConfig>& configs) {
    ConfigId config_id(file_id, _region_id);
    _configs[config_id].configs = configs;
}

bool RegionStatistics::GetConfigurations(int file_id, std::vector<CARTA::SetStatsRequirements_StatsConfig>& configs) {
    ConfigId config_id(file_id, _region_id);
    if (_configs.find(config_id) != _configs.end()) {
        configs = _configs.at(config_id).configs;
        return true;
    }
    return false;
}

std::vector<int> RegionStatistics::GetConfigFileIds(int file_id) {
    std::vector<int> file_ids;
    for (const auto& [config_id, _] : _configs) {
        if ((file_id == ALL_FILES) || (config_id.file_id == file_id)) {
            file_ids.push_back(config_id.file_id);
        }
    }
    return file_ids;
}

bool RegionStatistics::GetRegionStatsData(int file_id, std::shared_ptr<Frame> frame, const CARTA::SetStatsRequirements_StatsConfig& config,
    StokesRegion& stokes_region, CARTA::RegionStatsData& stats_data_message) {
    bool success(false);
    int z(stokes_region.stokes_source.z_range.from);
    int stokes(stokes_region.stokes_source.stokes);
    stats_data_message = Message::RegionStatsData(file_id, _region_id, z, stokes);

    // Set required stats types
    std::vector<CARTA::StatsType> required_stats;
    for (int i = 0; i < config.stats_types_size(); ++i) {
        required_stats.push_back(config.stats_types(i));
    }

    // Check cache
    CacheId cache_id = CacheId(file_id, _region_id, stokes, z);
    if (_cache.find(cache_id) != _cache.end()) {
        std::map<CARTA::StatsType, double> stats_results;
        if (_cache[cache_id].GetStats(stats_results)) {
            FillStatistics(stats_data_message, required_stats, stats_results); // Message helper
            return true;
        }
    }

    if (!stokes_region.image_region.isLCRegion()) {
        // region outside image: NaN results
        std::map<CARTA::StatsType, double> stats_results;
        for (const auto& carta_stat : required_stats) {
            if (carta_stat == CARTA::StatsType::NumPixels) {
                stats_results[carta_stat] = 0.0;
            } else {
                stats_results[carta_stat] = DOUBLE_NAN;
            }
        }
        FillStatistics(stats_data_message, required_stats, stats_results); // Message helper
        return true;
    }

    // Calculate stats
    bool per_z(false);
    std::map<CARTA::StatsType, std::vector<double>> stats_map;
    if (frame->GetRegionStats(stokes_region, required_stats, per_z, stats_map)) {
        // convert vector to single value in map
        std::map<CARTA::StatsType, double> stats_results;
        for (const auto& [type, statistic] : stats_map) {
            stats_results[type] = statistic[0];
        }

        // add values to message
        FillStatistics(stats_data_message, required_stats, stats_results); // Message helper

        // cache results
        _cache[cache_id] = StatsCache(stats_results);
        return true;
    }

    return false;
}

void RegionStatistics::ClearCache() {
    for (auto& cache : _cache) {
        cache.second.ClearStats();
    }
}

void RegionStatistics::ClearFileConfigsCache(int file_id) {
    for (auto it = _configs.begin(); it != _configs.end();) {
        if ((*it).first.file_id == file_id) {
            it = _configs.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = _cache.begin(); it != _cache.end();) {
        if ((*it).first.file_id == file_id) {
            it = _cache.erase(it);
        } else {
            ++it;
        }
    }
}
