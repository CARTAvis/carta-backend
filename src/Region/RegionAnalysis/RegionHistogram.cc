/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// RegionHistogram.cc: calculate histograms for a region in a frame

#include "RegionHistogram.h"

#include "ImageStats/StatsCalculator.h"
#include "RegionAnalysisUtil.h"
#include "Util/Image.h"
#include "Util/Message.h"

namespace carta {

RegionHistogram::RegionHistogram(int region_id, int file_id, const std::vector<CARTA::HistogramConfig>& configs) : _region_id(region_id) {
    SetConfigurations(file_id, configs);
}

void RegionHistogram::SetConfigurations(int file_id, const std::vector<CARTA::HistogramConfig>& configs) {
    std::vector<HistogramConfig> input_configs;
    for (const auto& config : configs) {
        HistogramConfig hist_config(config);
        input_configs.push_back(hist_config);
    }

    ConfigId config_id(file_id, _region_id);
    _configs[config_id].configs = input_configs;
}

bool RegionHistogram::GetConfigurations(int file_id, std::vector<HistogramConfig>& configs) {
    ConfigId config_id(file_id, _region_id);
    if (_configs.find(config_id) != _configs.end()) {
        configs = _configs.at(config_id).configs;
        return true;
    }
    return false;
}

std::vector<int> RegionHistogram::GetConfigFileIds(int file_id) {
    std::vector<int> file_ids;
    for (auto& config : _configs) {
        // File id -1 is for all files
        if ((file_id < 0) || (config.first.file_id == file_id)) {
            file_ids.push_back(config.first.file_id);
        }
    }
    return file_ids;
}

void RegionHistogram::FillHistogramDataParams(
    int file_id, StokesSource& stokes_source, const HistogramConfig& config, CARTA::RegionHistogramData& histogram_data_message) {
    int stokes(stokes_source.stokes);
    int z(stokes_source.z_range.from);
    histogram_data_message = Message::RegionHistogramData(file_id, _region_id, z, stokes, 1.0, config);
}

void RegionHistogram::AddDefaultHistogram(CARTA::RegionHistogramData& histogram_data_message) {
    auto* histogram = histogram_data_message.mutable_histograms();
    std::vector<int> histogram_bins(1, 0);
    FillHistogram(histogram, 1, 0.0, 0.0, histogram_bins, DOUBLE_NAN, DOUBLE_NAN); // Message helper
}

bool RegionHistogram::GetRegionHistogramData(int file_id, std::shared_ptr<Frame> frame, const HistogramConfig& config,
    std::shared_ptr<casacore::LCRegion> lcregion, StokesSource& stokes_source, CARTA::RegionHistogramData& histogram_data_message) {
    FillHistogramDataParams(file_id, stokes_source, config, histogram_data_message);
    if (!lcregion) {
        // Region outside image
        AddDefaultHistogram(histogram_data_message);
        return true;
    }

    int num_bins = GetNumBins(config, frame, lcregion); // from config or calculated from region shape

    // Check cache
    int stokes = stokes_source.stokes;
    int z = stokes_source.z_range.from;
    CacheId cache_id(file_id, _region_id, stokes, z);
    if (AddCachedHistogram(cache_id, config, num_bins, histogram_data_message)) {
        return true;
    }

    // Calculate stats and histogram
    auto stokes_slicer = GetRegionStokesSlicer(frame, lcregion, stokes_source);
    std::vector<float> region_data(stokes_slicer.slicer.length().product(), FLOAT_NAN);
    if (!frame->GetSlicerData(stokes_slicer, region_data.data())) {
        return false;
    }

    BasicStats<float> stats;
    if (!_cache[cache_id].GetBasicStats(stats)) {
        CalcBasicStats(stats, region_data.data(), region_data.size());
        _cache[cache_id].SetBasicStats(stats);
    }
    auto bounds = config.GetBounds(stats);
    Histogram calculated_histogram = CalcHistogram(num_bins, bounds, region_data.data(), region_data.size());
    _cache[cache_id].SetHistogram(num_bins, calculated_histogram);

    // Add histogram to histogram data message
    auto* histogram_submessage = histogram_data_message.mutable_histograms();
    FillHistogram(histogram_submessage, stats, calculated_histogram); // Message helper
    return true;
}

int RegionHistogram::GetNumBins(const HistogramConfig& config, std::shared_ptr<Frame> frame, std::shared_ptr<casacore::LCRegion> lcregion) {
    int num_bins(config.num_bins);
    if (num_bins == AUTO_BIN_SIZE) {
        casacore::IPosition region_shape = lcregion->shape();
        num_bins = int(std::max(sqrt(region_shape(0) * region_shape(1)), 2.0));
    }
    return num_bins;
}

bool RegionHistogram::AddCachedHistogram(
    CacheId& cache_id, const HistogramConfig& config, int num_bins, CARTA::RegionHistogramData& histogram_data) {
    bool success(false);
    // check cache
    if (_cache.find(cache_id) != _cache.end()) {
        BasicStats<float> stats;
        if (_cache[cache_id].GetBasicStats(stats)) {
            auto bounds = config.GetBounds(stats);
            Histogram cached_histogram;
            if (_cache[cache_id].GetHistogram(num_bins, bounds, cached_histogram)) {
                auto* histogram_submessage = histogram_data.mutable_histograms();
                FillHistogram(histogram_submessage, stats, cached_histogram); // Message helper
                success = true;
            }
        }
    }
    return success;
}

void RegionHistogram::ClearCache() {
    for (auto& cache : _cache) {
        cache.second.ClearHistograms();
    }
}

void RegionHistogram::ClearFileConfigsCache(int file_id) {
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

} // namespace carta
