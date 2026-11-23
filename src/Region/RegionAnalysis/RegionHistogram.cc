/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// RegionHistogram.cc: calculate histograms for a region in a frame

#include "RegionHistogram.h"

#include "ImageStats/StatsCalculator.h"
#include "Util/File.h"
#include "Util/Image.h"
#include "Util/Message.h"

namespace carta {

RegionHistogram::RegionHistogram(int region_id, int file_id, const std::vector<CARTA::HistogramConfig>& configs) : _region_id(region_id) {
    SetConfigurations(file_id, configs);
}

void RegionHistogram::SetConfigurations(int file_id, const std::vector<CARTA::HistogramConfig>& configs) {
    std::vector<HistogramConfig> input_configs; // config structs
    for (const auto& config : configs) {
        HistogramConfig hist_config(config);
        input_configs.push_back(hist_config);
    }

    ConfigId config_id(file_id, _region_id);
    std::lock_guard<std::mutex> config_guard(_config_mutex);
    _configs[config_id].configs = input_configs;
}

bool RegionHistogram::GetConfigurations(int file_id, std::vector<HistogramConfig>& configs) {
    ConfigId config_id(file_id, _region_id);
    std::lock_guard<std::mutex> config_guard(_config_mutex);
    if (_configs.find(config_id) != _configs.end()) {
        configs = _configs.at(config_id).configs;
        return true;
    }
    return false;
}

std::vector<int> RegionHistogram::GetConfigFileIds(int file_id) {
    std::lock_guard<std::mutex> config_guard(_config_mutex);
    std::vector<int> file_ids;
    for (const auto& [config_id, _] : _configs) {
        if ((file_id == ALL_FILES) || (config_id.file_id == file_id)) {
            file_ids.push_back(config_id.file_id);
        }
    }
    return file_ids;
}

void RegionHistogram::FillHistogramDataParams(
    int file_id, StokesSource& stokes_source, const HistogramConfig& config, CARTA::RegionHistogramData& histogram_data_message) {
    int stokes_index(stokes_source.stokes);
    int z(stokes_source.z_range.from);
    histogram_data_message = Message::RegionHistogramData(file_id, _region_id, z, stokes_index, 1.0, config);
}

void RegionHistogram::AddDefaultHistogram(CARTA::RegionHistogramData& histogram_data_message) {
    auto* histogram = histogram_data_message.mutable_histograms();
    std::vector<int> histogram_bins(1, 0);
    FillHistogram(histogram, 1, 0.0, 0.0, histogram_bins, DOUBLE_NAN, DOUBLE_NAN); // Message helper
}

bool RegionHistogram::GetRegionHistogramData(int file_id, std::shared_ptr<Frame> frame, const HistogramConfig& config,
    StokesRegion& stokes_region, CARTA::RegionHistogramData& histogram_data_message) {
    auto image_region = stokes_region.image_region;
    auto stokes_source = stokes_region.stokes_source;

    FillHistogramDataParams(file_id, stokes_source, config, histogram_data_message);
    if (!image_region.isLCRegion()) {
        // Region outside image
        AddDefaultHistogram(histogram_data_message);
        return true;
    }

    int num_bins = GetNumBins(config, frame, image_region); // config setting, or calculated from region shape

    // Check cache
    int stokes_index = stokes_source.stokes;
    int z = stokes_source.z_range.from;
    CacheId cache_id(file_id, _region_id, stokes_index, z);
    std::uint64_t cache_generation;
    BasicStats<float> stats;
    bool has_basic_stats(false);
    {
        std::lock_guard<std::mutex> cache_guard(_cache_mutex);
        cache_generation = _cache_generation;
        auto cache_it = _cache.find(cache_id);
        if (cache_it != _cache.end() && cache_it->second.GetBasicStats(stats)) {
            has_basic_stats = true;
            auto bounds = config.GetBounds(stats);
            Histogram cached_histogram;
            if (cache_it->second.GetHistogram(num_bins, bounds, cached_histogram)) {
                auto* histogram_submessage = histogram_data_message.mutable_histograms();
                FillHistogram(histogram_submessage, stats, cached_histogram); // Message helper
                return true;
            }
        }
    }

    std::vector<float> region_data;
    if (!frame->GetRegionData(stokes_region, region_data, false)) {
        return false;
    }
    if (!has_basic_stats) {
        CalcBasicStats(stats, region_data.data(), region_data.size());
    }

    auto bounds = config.GetBounds(stats);
    Histogram calculated_histogram = CalcHistogram(num_bins, bounds, region_data.data(), region_data.size());

    // Add histogram to histogram data message
    auto* histogram_submessage = histogram_data_message.mutable_histograms();
    FillHistogram(histogram_submessage, stats, calculated_histogram); // Message helper

    {
        std::lock_guard<std::mutex> cache_guard(_cache_mutex);
        if (cache_generation == _cache_generation) {
            auto& cache = _cache[cache_id];
            cache.SetBasicStats(stats);
            cache.SetHistogram(num_bins, calculated_histogram);
        }
    }
    return true;
}

int RegionHistogram::GetNumBins(const HistogramConfig& config, std::shared_ptr<Frame> frame, casacore::ImageRegion& image_region) {
    int num_bins(config.num_bins);
    if (num_bins == AUTO_BIN_SIZE) {
        casacore::IPosition region_shape = image_region.asLCRegionPtr()->shape();
        num_bins = int(std::max(sqrt(region_shape(0) * region_shape(1)), 2.0));
    }
    return num_bins;
}

void RegionHistogram::ClearCache() {
    std::lock_guard<std::mutex> cache_guard(_cache_mutex);
    ++_cache_generation;
    for (auto& cache : _cache) {
        cache.second.ClearHistograms();
    }
}

void RegionHistogram::ClearFileConfigsCache(int file_id) {
    // Configs and cache have separate locks; never hold both (no lock ordering needed).
    {
        std::lock_guard<std::mutex> config_guard(_config_mutex);
        for (auto it = _configs.begin(); it != _configs.end();) {
            if ((*it).first.file_id == file_id) {
                it = _configs.erase(it);
            } else {
                ++it;
            }
        }
    }

    {
        std::lock_guard<std::mutex> cache_guard(_cache_mutex);
        ++_cache_generation;
        for (auto it = _cache.begin(); it != _cache.end();) {
            if ((*it).first.file_id == file_id) {
                it = _cache.erase(it);
            } else {
                ++it;
            }
        }
    }
}

} // namespace carta
