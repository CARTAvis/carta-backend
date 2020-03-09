// RegionDataHandler.cc: handle requirements and data streams for regions

#include "RegionHandler.h"

#include <thread>

#include "../InterfaceConstants.h"
#include "../Util.h"

using namespace carta;

RegionHandler::RegionHandler() : _z_profile_count(0) {}

// ********************************************************************
// Region handling

int RegionHandler::GetNextRegionId() {
    // returns maximum id + 1; start at 1 if no regions set
    int max_id(0);
    if (!_regions.empty()) {
        for (auto& region : _regions) {
            if (region.first > max_id) {
                max_id = region.first;
            }
        }
    }
    return max_id + 1;
}

bool RegionHandler::SetRegion(int& region_id, int file_id, const std::string& name, CARTA::RegionType type,
    const std::vector<CARTA::Point>& points, float rotation, const casacore::CoordinateSystem& csys) {
    // Set region params for region id; if id < 0, create new id
    bool valid_region(false);
    if (_regions.count(region_id)) {
        _regions.at(region_id)->UpdateState(file_id, name, type, points, rotation, csys);
        valid_region = _regions.at(region_id)->IsValid();
    } else {
        if (region_id < 0) {
            region_id = GetNextRegionId();
        }
        auto region = std::unique_ptr<Region>(new Region(file_id, name, type, points, rotation, csys));
        valid_region = region->IsValid();
        if (valid_region) {
            _regions[region_id] = std::move(region);
        }
    }
    return valid_region;
}

bool RegionHandler::RegionChanged(int region_id) {
    if (!_regions.count(region_id)) {
        return false;
    }
    return _regions.at(region_id)->RegionChanged();
}

void RegionHandler::RemoveRegion(int region_id) {
    // call destructor and erase from map
    if (!RegionSet(region_id)) {
        return;
    }

    CancelJobs(region_id);

    if (region_id == ALL_REGIONS) {
        _regions.clear();
        ClearRequirements(region_id);
    } else if (_regions.count(region_id)) {
        _regions.erase(region_id);
        ClearRequirements(region_id);
    }
}

bool RegionHandler::RegionSet(int region_id) {
    // Check whether a particular region is set or any regions are set
    if (region_id == ALL_REGIONS) {
        return _regions.size();
    } else {
        return _regions.count(region_id);
    }
}

// ********************************************************************
// Region job handling

void RegionHandler::CancelJobs(int region_id) {
    // cancel jobs for given region which has been removed
    if (region_id == ALL_REGIONS) {
        CancelAllJobs();
    } else {
        // TODO: do something for region
    }
}

bool RegionHandler::ShouldCancelJob(int region_id) {
    // check if job should be canceled for region or all regions
    bool cancel = _cancel_all_jobs;

    if (!cancel) {
        // TODO: check something for region
    }
    return cancel;
}

void RegionHandler::CancelAllJobs() {
    // notify that handler will be deleted
    _cancel_all_jobs = true;

    while (_z_profile_count) {
        // wait for spectral profiles to complete to avoid crash
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void RegionHandler::IncreaseZProfileCount(int file_id, int region_id) {
    if (region_id < 0) {
        // TODO: all regions with this file_id in requirements
        // TODO: ++_z_profile_count; for each region
    } else if (_regions.count(region_id)) {
        // TODO: _regions.at(region_id)->IncreaseZProfileCount();
        ++_z_profile_count;
    }
}

void RegionHandler::DecreaseZProfileCount(int file_id, int region_id) {
    if (region_id < 0) {
        // TODO: all regions with this file_id in requirements
        // TODO: --_z_profile_count; for each region
    } else if (_regions.count(region_id)) {
        // TODO: _regions.at(region_id)->DecreaseZProfileCount();
        --_z_profile_count;
    }
}

// ********************************************************************
// Region requirements handling

bool RegionHandler::SetHistogramRequirements(
    int region_id, int file_id, std::shared_ptr<Frame> frame, const std::vector<CARTA::SetHistogramRequirements_HistogramConfig>& configs) {
    // Set histogram requirements for given region and file
    if (configs.empty() && !_regions.count(region_id)) {
        // frontend clears requirements after region removed, prevent error in log
        return true;
    }

    if (_regions.count(region_id)) {
        // Save frame pointer
        _frames[file_id] = frame;

        // Make HistogramConfig vector of requirements
        std::vector<HistogramConfig> input_configs;
        for (auto& config : configs) {
            HistogramConfig hist_config;
            hist_config.channel = config.channel();
            hist_config.num_bins = config.num_bins();
            input_configs.push_back(hist_config);
        }

        // Replace file_id requirements
        if (_histogram_req.count(region_id)) {
            for (auto& region_hist_config : _histogram_req[region_id]) {
                if (region_hist_config.file_id == file_id) {
                    region_hist_config.configs = input_configs;
                    return true;
                }
            }
        }

        // Add new file_id requirements
        RegionHistogramConfig rhconfig;
        rhconfig.file_id = file_id;
        rhconfig.configs = input_configs;
        _histogram_req[region_id].push_back(rhconfig);
        return true;
    }
    return false;
}

bool RegionHandler::SetSpectralRequirements(
    int region_id, int file_id, std::shared_ptr<Frame> frame, const std::vector<CARTA::SetSpectralRequirements_SpectralConfig>& configs) {
    // Set spectral profile requirements for given region and file
    if (configs.empty() && !_regions.count(region_id)) {
        // frontend clears requirements after region removed, prevent error in log
        return true;
    }

    if (_regions.count(region_id)) {
        // Save frame pointer
        _frames[file_id] = frame;

        // Make SpectralConfig vector of requirements
        std::vector<SpectralConfig> input_configs;
        for (auto& config : configs) {
            std::string coordinate(config.coordinate());
            int axis, stokes;
            ConvertCoordinateToAxes(coordinate, axis, stokes);
            std::vector<int> stats = std::vector<int>(config.stats_types().begin(), config.stats_types().end());
            SpectralConfig spec_config(coordinate, stokes, stats);
            input_configs.push_back(spec_config);
        }

        // Replace file_id requirements
        if (_spectral_req.count(region_id)) {
            for (auto& region_spec_config : _spectral_req[region_id]) {
                if (region_spec_config.file_id == file_id) {
                    region_spec_config.configs = input_configs;
                    return true;
                }
            }
        }

        // Add new file_id requirements
        RegionSpectralConfig rsconfig;
        rsconfig.file_id = file_id;
        rsconfig.configs = input_configs;
        _spectral_req[region_id].push_back(rsconfig);
        return true;
    }
    return false;
}

bool RegionHandler::SetStatsRequirements(int region_id, int file_id, std::shared_ptr<Frame> frame, const std::vector<int>& stats_types) {
    // Set stats data requirements for given region and file
    if (stats_types.empty() && !_regions.count(region_id)) {
        // frontend clears requirements after region removed, prevent error in log
        return true;
    }

    if (_regions.count(region_id)) {
        // Save frame pointer
        _frames[file_id] = frame;

        // Replace file_id requirements
        if (_stats_req.count(region_id)) {
            for (auto& region_stats_config : _stats_req[region_id]) {
                if (region_stats_config.file_id == file_id) {
                    region_stats_config.stats_types = stats_types;
                    return true;
                }
            }
        }

        // Add new file_id requirements
        RegionStatsConfig rsconfig;
        rsconfig.file_id = file_id;
        rsconfig.stats_types = stats_types;
        _stats_req[region_id].push_back(rsconfig);
        return true;
    }
    return false;
}

void RegionHandler::ClearRequirements(int region_id) {
    // clear requirements for a specific region or for all regions when closed
    if (region_id == ALL_REGIONS) {
        _histogram_req.clear();
        _spectral_req.clear();
        _stats_req.clear();
    } else {
        _histogram_req.erase(region_id);
        _spectral_req.erase(region_id);
        _stats_req.erase(region_id);
    }
}

void RegionHandler::RemoveFrame(int file_id) {
    if (file_id == ALL_FILES) {
        _frames.clear();
    } else if (_frames.count(file_id)) {
        _frames.erase(file_id);
    }
}

bool RegionHandler::FrameSet(int file_id) {
    // Check whether a particular file is set or any files are set
    if (file_id == ALL_FILES) {
        return _frames.size();
    } else {
        return _frames.count(file_id);
    }
}

// ********************************************************************
// Region data streams:
// These always use a callback since there may be multiple region/file requirements
// region_id > 0 file_id >= 0   update data for specified region/file
// region_id > 0 file_id < 0    update data for all files in region's requirements (region changed)
// region_id < 0 file_id >= 0   update data for all regions with file_id (chan/stokes changed)
// region_id < 0 file_id < 0    not allowed (all regions for all files?)
// region_id = 0                not allowed (cursor region handled by Frame)

bool RegionHandler::FillRegionHistogramData(std::function<void(CARTA::RegionHistogramData histogram_data)> cb, int region_id, int file_id) {
    // Fill histogram data for given region and file

    // Check error conditions and preconditions
    if (((region_id < 0) && (file_id < 0)) || (region_id == 0)) {
        return false;
    }
    if (!RegionSet(region_id)) { // no Region(s) for this id
        return false;
    }
    if (!FrameSet(file_id)) { // no Frame(s) for this id
        return false;
    }
    if ((region_id > 0) && !_histogram_req.count(region_id)) { // no requirements for this region
        return false;
    }

    bool message_filled(false);
    if (region_id > 0) {
        // Fill histograms for specific region with file_id requirement (specific file_id or all files)
        for (auto region_config : _histogram_req[region_id]) { // RegionHistogramConfig in vector
            if ((file_id == ALL_FILES) || (region_config.file_id == file_id)) {
                int channel(_frames.at(region_config.file_id)->CurrentChannel());
                int stokes(_frames.at(region_config.file_id)->CurrentStokes());

                // return histogram for this requirement
                CARTA::RegionHistogramData histogram_message;
                histogram_message.set_file_id(region_config.file_id);
                histogram_message.set_region_id(region_id);
                histogram_message.set_stokes(stokes);

                for (auto& hist_config : region_config.configs) { // HistogramConfig in vector
                    // TODO: get basic stats and region histogram
                    int num_bins = hist_config.num_bins;

                    // TODO: complete Histogram submessage
                    auto histogram = histogram_message.add_histograms();
                    histogram->set_channel(channel);
                    histogram->set_num_bins(num_bins);
                    // set bin_width, first_bin_center, bins, mean, std_dev
                }
                // callback to send data
                cb(histogram_message);
                message_filled = true;
            }
        }
    } else {
        // (region_id < 0) Fill histograms for all regions with specific file_id requirement
        int channel(_frames.at(file_id)->CurrentChannel());
        int stokes(_frames.at(file_id)->CurrentStokes());

        for (auto& req : _histogram_req) {           // <region_id, vector<RegionHistogramConfig>
            for (auto& region_config : req.second) { // RegionHistogramConfig in vector
                if (region_config.file_id == file_id) {
                    CARTA::RegionHistogramData histogram_message;
                    histogram_message.set_file_id(file_id);
                    histogram_message.set_region_id(req.first);
                    histogram_message.set_stokes(stokes);

                    for (auto& hist_config : region_config.configs) { // HistogramConfig in vector
                        // TODO: get basic stats and region histogram
                        int num_bins = hist_config.num_bins;

                        // TODO: complete Histogram submessage
                        auto histogram = histogram_message.add_histograms();
                        histogram->set_channel(channel);
                        histogram->set_num_bins(num_bins);
                        // set bin_width, first_bin_center, bins, mean, std_dev
                    }
                    // callback to send data
                    cb(histogram_message);
                    message_filled = true;
                }
            }
        }
    }
    return message_filled;
}

bool RegionHandler::FillSpectralProfileData(
    std::function<void(CARTA::SpectralProfileData profile_data)> cb, int region_id, int file_id, bool stokes_changed) {
    // Fill spectral profiles for given region and file
    // Check error conditions and preconditions
    if (((region_id < 0) && (file_id < 0)) || (region_id == 0)) {
        return false;
    }
    if (!RegionSet(region_id)) { // no Region(s) for this id
        return false;
    }
    if (!FrameSet(file_id)) { // no Frame(s) for this id
        return false;
    }
    if ((region_id > 0) && !_spectral_req.count(region_id)) { // no requirements for this region
        return false;
    }

    bool message_filled(false);
    if (region_id > 0) {
        // Fill spectral profile for specific region with file_id requirement (specific file_id or all files)
        for (auto region_config : _spectral_req[region_id]) { // RegionSpectralConfig in vector
            if ((file_id == ALL_FILES) || (region_config.file_id == file_id)) {
                // return profile for this requirement
                CARTA::SpectralProfileData profile_message;
                profile_message.set_file_id(region_config.file_id);
                profile_message.set_region_id(region_id);
                // TODO: set progress

                for (auto& spectral_config : region_config.configs) { // SpectralConfig in vector
                    // handle stokes requirement
                    int stokes(spectral_config.stokes_index);
                    if ((stokes != CURRENT_STOKES) && stokes_changed) { // do not update for specific stokes when stokes changes
                        continue;
                    }
                    if (stokes == CURRENT_STOKES) {
                        stokes = _frames.at(region_config.file_id)->CurrentStokes();
                    }
                    profile_message.set_stokes(stokes);

                    // TODO: get spectral profile data

                    for (auto stats_type : spectral_config.stats_types) {
                        // Add SpectralProfile to message
                        auto spectral_profile = profile_message.add_profiles();
                        spectral_profile->set_coordinate(spectral_config.coordinate);
                        spectral_profile->set_stats_type(static_cast<CARTA::StatsType>(stats_type));
                        // TODO: add spectral profile values for this stat
                    }
                }
                // callback to send data
                cb(profile_message);
                message_filled = true;
            }
        }
    } else {
        // (region_id < 0) Fill spectral profile for all regions with specific file_id requirement
        for (auto& req : _spectral_req) {            // <region_id, vector<RegionSpectralConfig>
            for (auto& region_config : req.second) { // RegionSpectralConfig in vector
                if (region_config.file_id == file_id) {
                    // return profile for this requirement
                    CARTA::SpectralProfileData profile_message;
                    profile_message.set_file_id(file_id);
                    profile_message.set_region_id(req.first);
                    // TODO: set progress

                    for (auto& spectral_config : region_config.configs) { // SpectralConfig in vector
                        // handle stokes requirement
                        int stokes(spectral_config.stokes_index);
                        if ((stokes != CURRENT_STOKES) && stokes_changed) { // do not update for specific stokes when stokes changes
                            continue;
                        }
                        if (stokes == CURRENT_STOKES) {
                            stokes = _frames.at(region_config.file_id)->CurrentStokes();
                        }
                        profile_message.set_stokes(stokes);

                        // TODO: get spectral profile data

                        for (auto stats_type : spectral_config.stats_types) {
                            // Add SpectralProfile to message
                            auto spectral_profile = profile_message.add_profiles();
                            spectral_profile->set_coordinate(spectral_config.coordinate);
                            spectral_profile->set_stats_type(static_cast<CARTA::StatsType>(stats_type));
                            // TODO: add spectral profile values for this stat
                        }
                    }
                    // callback to send data
                    cb(profile_message);
                    message_filled = true;
                }
            }
        }
    }
    return message_filled;
}

bool RegionHandler::FillRegionStatsData(std::function<void(CARTA::RegionStatsData stats_data)> cb, int region_id, int file_id) {
    // Fill stats data for given region and file
    // Check error conditions and preconditions
    if (((region_id < 0) && (file_id < 0)) || (region_id == 0)) {
        return false;
    }
    if (!RegionSet(region_id)) { // no Region(s) for this region_id
        return false;
    }
    if (!FrameSet(file_id)) { // no Frame(s) for this file_id
        return false;
    }
    if ((region_id > 0) && !_stats_req.count(region_id)) { // no requirements for this region
        return false;
    }

    bool message_filled(false);
    if (region_id > 0) {
        // Fill stats data for specific region with file_id requirement (specific file_id or all files)
        for (auto region_config : _stats_req[region_id]) { // RegionStatsConfig in vector
            auto config_file_id(region_config.file_id);
            if ((file_id == ALL_FILES) || (config_file_id == file_id)) {
                int channel(_frames.at(config_file_id)->CurrentChannel());
                int stokes(_frames.at(config_file_id)->CurrentStokes());

                // return stats for this requirement
                CARTA::RegionStatsData stats_message;
                stats_message.set_file_id(config_file_id);
                stats_message.set_region_id(region_id);
                stats_message.set_channel(channel);
                stats_message.set_stokes(stokes);

                std::unordered_map<CARTA::StatsType, std::vector<double>> stats_values;
                // TODO: get region stats values
                // FillStatisticsValuesFromMap(stats_message, region_config.stats_types, stats_values);

                // send data
                cb(stats_message);
                message_filled = true;
            }
        }
    } else {
        // (region_id < 0) Fill stats data for all regions with specific file_id requirement
        int channel(_frames.at(file_id)->CurrentChannel());
        int stokes(_frames.at(file_id)->CurrentStokes());

        for (auto& req : _stats_req) {               // <region_id, vector<RegionStatsConfig>
            for (auto& region_config : req.second) { // RegionStatsConfig in vector
                if (region_config.file_id == file_id) {
                    // return stats for this requirement
                    CARTA::RegionStatsData stats_message;
                    stats_message.set_file_id(file_id);
                    stats_message.set_region_id(req.first);
                    stats_message.set_channel(channel);
                    stats_message.set_stokes(stokes);

                    // Add StatisticsValue[]
                    std::unordered_map<CARTA::StatsType, std::vector<double>> stats_values;
                    // TODO: get region stats values
                    // FillStatisticsValuesFromMap(stats_message, region_config.stats_types, stats_values);

                    // send data
                    cb(stats_message);
                    message_filled = true;
                }
            }
        }
    }
    return message_filled;
}
