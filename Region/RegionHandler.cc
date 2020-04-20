// RegionDataHandler.cc: handle requirements and data streams for regions

#include "RegionHandler.h"

#include <chrono>

#include <casacore/lattices/LRegions/LCBox.h>
#include <casacore/lattices/LRegions/LCIntersection.h>

#include "../ImageStats/StatsCalculator.h"
#include "../InterfaceConstants.h"
#include "../Util.h"
#include "RegionImportExport.h"

using namespace carta;

RegionHandler::RegionHandler(bool verbose) : _verbose(verbose), _z_profile_count(0) {}

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
        if (_regions.at(region_id)->RegionChanged()) {
            UpdateNewSpectralRequirements(region_id); // set all req "new"
            ClearRegionCache(region_id);
        }
    } else {
        if (region_id < 0) {
            region_id = GetNextRegionId();
        }
        auto region = std::shared_ptr<Region>(new Region(file_id, name, type, points, rotation, csys));
        if (region && region->IsValid()) {
            _regions[region_id] = std::move(region);
            valid_region = true;
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

    if (region_id == ALL_REGIONS) {
        for (auto& region : _regions) {
            region.second->DisconnectCalled();
        }
        _regions.clear();
    } else if (_regions.count(region_id)) {
        _regions.at(region_id)->DisconnectCalled();
        _regions.erase(region_id);
    }
    RemoveRegionRequirementsCache(region_id);
}

bool RegionHandler::RegionSet(int region_id) {
    // Check whether a particular region is set or any regions are set
    if (region_id == ALL_REGIONS) {
        return _regions.size();
    } else {
        return _regions.count(region_id) && _regions.at(region_id)->IsConnected();
    }
}
void RegionHandler::ImportRegion(int file_id, std::shared_ptr<Frame> frame, CARTA::FileType region_file_type,
    const std::string& region_file, bool file_is_filename, CARTA::ImportRegionAck& import_ack) {
    // Set regions from region file
    const casacore::CoordinateSystem csys = frame->CoordinateSystem();
    const casacore::IPosition shape = frame->ImageShape();
    RegionImportExport importer = RegionImportExport(region_file, region_file_type, csys, shape, file_is_filename);

    // Get region states from parser or error message
    std::string error;
    std::vector<RegionState> region_states = importer.GetImportedRegions(file_id, error);
    if (region_states.empty()) {
        import_ack.set_success(false);
        import_ack.set_message(error);
        import_ack.add_regions();
        return;
    }

    // Set reference file pointer
    _frames[file_id] = frame;

    // Set regions and complete message
    import_ack.set_success(true);
    import_ack.set_message(error);
    int region_id = GetNextRegionId();
    for (auto& region_state : region_states) {
        auto region = std::shared_ptr<Region>(new Region(region_state, csys));
        if (region && region->IsValid()) {
            _regions[region_id] = std::move(region);

            // Set RegionProperties and its RegionInfo
            auto region_properties = import_ack.add_regions();
            region_properties->set_region_id(region_id);
            auto region_info = region_properties->mutable_region_info();
            region_info->set_region_name(region_state.name);
            region_info->set_region_type(region_state.type);
            *region_info->mutable_control_points() = {region_state.control_points.begin(), region_state.control_points.end()};
            region_info->set_rotation(region_state.rotation);

            // increment region id for next region
            region_id++;
        }
    }
}

void RegionHandler::ExportRegion(int file_id, std::shared_ptr<Frame> frame, CARTA::FileType region_file_type,
    CARTA::CoordinateType coord_type, std::vector<int>& region_ids, std::string& filename, CARTA::ExportRegionAck& export_ack) {
    // Export regions to given filename, or return export file contents in ack

    // Check if regions to export
    if (region_ids.empty()) {
        export_ack.set_success(false);
        export_ack.set_message("Export failed: no regions requested.");
        export_ack.add_contents();
        return;
    }

    // Check export file before creating file contents
    if (!filename.empty()) {
        casacore::File export_file(filename);
        if (!export_file.canCreate()) {
            export_ack.set_success(false);
            export_ack.set_message("Export region failed: cannot create file.");
            export_ack.add_contents();
            return;
        }
    }

    // Response fields
    bool success(false);
    std::string message;

    // TODO: RegionImportExport

    export_ack.set_success(success);
    export_ack.set_message(message);
}

// ********************************************************************
// Frame handling

bool RegionHandler::FrameSet(int file_id) {
    // Check whether a particular file is set or any files are set
    if (file_id == ALL_FILES) {
        return _frames.size();
    } else {
        return _frames.count(file_id) && _frames.at(file_id)->IsConnected();
    }
}

void RegionHandler::RemoveFrame(int file_id) {
    if (file_id == ALL_FILES) {
        _frames.clear();
        RemoveRegion(ALL_REGIONS); // removes all regions, requirements, and caches
    } else if (_frames.count(file_id)) {
        _frames.erase(file_id);
        RemoveFileRequirementsCache(file_id);
    }
}

// ********************************************************************
// Region requirements handling

bool RegionHandler::SetHistogramRequirements(
    int region_id, int file_id, std::shared_ptr<Frame> frame, const std::vector<CARTA::SetHistogramRequirements_HistogramConfig>& configs) {
    // Set histogram requirements for given region and file
    if (configs.empty() && !RegionSet(region_id)) {
        // Frontend clears requirements after region removed, prevent error in log by returning true.
        return true;
    }

    if (_regions.count(region_id)) {
        // Save frame pointer
        _frames[file_id] = frame;

        // Make HistogramConfig vector of requirements
        std::vector<HistogramConfig> input_configs;
        for (auto& config : configs) {
            HistogramConfig hist_config(config.channel(), config.num_bins());
            input_configs.push_back(hist_config);
        }

        // Set requirements
        ConfigId config_id(file_id, region_id);
        _histogram_req[config_id].configs = input_configs;
        return true;
    }

    return false;
}

bool RegionHandler::SetSpectralRequirements(int region_id, int file_id, std::shared_ptr<Frame> frame,
    const std::vector<CARTA::SetSpectralRequirements_SpectralConfig>& spectral_profiles) {
    // Set spectral profile requirements for given region and file
    if (spectral_profiles.empty() && !RegionSet(region_id)) {
        // Frontend clears requirements after region removed, prevent error in log by returning true.
        return true;
    }

    if (_regions.count(region_id)) {
        // Save frame pointer
        _frames[file_id] = frame;

        // Make SpectralConfig vector of requirements
        ConfigId config_id(file_id, region_id);
        for (auto& profile : spectral_profiles) {
            std::string profile_coordinate(profile.coordinate());
            int axis, stokes;
            ConvertCoordinateToAxes(profile_coordinate, axis, stokes);

            std::vector<CARTA::StatsType> required_stats;
            for (size_t i = 0; i < profile.stats_types_size(); ++i) {
                required_stats.push_back(profile.stats_types(i));
            }

            // Set requirements
            bool found_config(false);
            for (auto& config : _spectral_req[config_id].configs) {
                if (config.coordinate == profile_coordinate) {
                    // this also sets new_stats (diff of existing stats)
                    config.SetNewRequirements(required_stats);
                    found_config = true;
                    break;
                }
            }
            if (!found_config) {
                SpectralConfig spec_config(profile_coordinate, stokes, required_stats);
                _spectral_req[config_id].configs.push_back(spec_config);
            }
        }

        return true;
    }

    return false;
}

bool RegionHandler::HasSpectralRequirements(int region_id, int file_id, int stokes) {
    // Search _spectral_req for given file, region, and stokes; used to check for cancellation
    ConfigId config_id(file_id, region_id);
    std::vector<SpectralConfig> spectral_configs = _spectral_req[config_id].configs;
    for (auto& config : spectral_configs) {
        if (config.stokes == stokes) {
            return true;
        }
    }

    return false;
}

void RegionHandler::UpdateNewSpectralRequirements(int region_id) {
    // Set all requirements "new" when region changes
    for (auto& req : _spectral_req) {
        if (req.first.region_id == region_id) {
            for (auto& spec_config : req.second.configs) {
                spec_config.SetAllNewStats();
            }
        }
    }
}

bool RegionHandler::SetStatsRequirements(
    int region_id, int file_id, std::shared_ptr<Frame> frame, const std::vector<CARTA::StatsType>& stats_types) {
    // Set stats data requirements for given region and file
    if (stats_types.empty() && !RegionSet(region_id)) {
        // frontend clears requirements after region removed, prevent error in log
        return true;
    }

    if (_regions.count(region_id)) {
        // Save frame pointer
        _frames[file_id] = frame;

        // Set requirements
        ConfigId config_id(file_id, region_id);
        _stats_req[config_id].stats_types = stats_types;
        return true;
    }
    return false;
}

void RegionHandler::RemoveRegionRequirementsCache(int region_id) {
    // Clear requirements and cache for a specific region or for all regions when closed
    if (region_id == ALL_REGIONS) {
        _histogram_req.clear();
        _spectral_req.clear();
        _stats_req.clear();
        _histogram_cache.clear();
        _spectral_cache.clear();
        _stats_cache.clear();
    } else {
        // Iterate through requirements and remove those for given region_id
        for (auto it = _histogram_req.begin(); it != _histogram_req.end();) {
            if ((*it).first.region_id == region_id) {
                it = _histogram_req.erase(it);
            } else {
                ++it;
            }
        }

        for (auto it = _spectral_req.begin(); it != _spectral_req.end();) {
            if ((*it).first.region_id == region_id) {
                it = _spectral_req.erase(it);
            } else {
                ++it;
            }
        }

        for (auto it = _stats_req.begin(); it != _stats_req.end();) {
            if ((*it).first.region_id == region_id) {
                it = _stats_req.erase(it);
            } else {
                ++it;
            }
        }

        // Iterate through cache and remove those for given region_id
        for (auto it = _histogram_cache.begin(); it != _histogram_cache.end();) {
            if ((*it).first.region_id == region_id) {
                it = _histogram_cache.erase(it);
            } else {
                ++it;
            }
        }

        for (auto it = _spectral_cache.begin(); it != _spectral_cache.end();) {
            if ((*it).first.region_id == region_id) {
                it = _spectral_cache.erase(it);
            } else {
                ++it;
            }
        }

        for (auto it = _stats_cache.begin(); it != _stats_cache.end();) {
            if ((*it).first.region_id == region_id) {
                it = _stats_cache.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void RegionHandler::RemoveFileRequirementsCache(int file_id) {
    // Clear requirements and cache for a specific file or for all files when closed
    if (file_id == ALL_FILES) {
        _histogram_req.clear();
        _spectral_req.clear();
        _stats_req.clear();
        _histogram_cache.clear();
        _spectral_cache.clear();
        _stats_cache.clear();
    } else {
        // Iterate through requirements and remove those for given file_id
        for (auto it = _histogram_req.begin(); it != _histogram_req.end();) {
            if ((*it).first.file_id == file_id) {
                it = _histogram_req.erase(it);
            } else {
                ++it;
            }
        }

        for (auto it = _spectral_req.begin(); it != _spectral_req.end();) {
            if ((*it).first.file_id == file_id) {
                it = _spectral_req.erase(it);
            } else {
                ++it;
            }
        }

        for (auto it = _stats_req.begin(); it != _stats_req.end();) {
            if ((*it).first.file_id == file_id) {
                it = _stats_req.erase(it);
            } else {
                ++it;
            }
        }

        // Iterate through cache and remove those for given file_id
        for (auto it = _histogram_cache.begin(); it != _histogram_cache.end();) {
            if ((*it).first.file_id == file_id) {
                it = _histogram_cache.erase(it);
            } else {
                ++it;
            }
        }

        for (auto it = _spectral_cache.begin(); it != _spectral_cache.end();) {
            if ((*it).first.file_id == file_id) {
                it = _spectral_cache.erase(it);
            } else {
                ++it;
            }
        }

        for (auto it = _stats_cache.begin(); it != _stats_cache.end();) {
            if ((*it).first.file_id == file_id) {
                it = _stats_cache.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void RegionHandler::ClearRegionCache(int region_id) {
    // Remove cached data when region changes
    for (auto& hcache : _histogram_cache) {
        if (hcache.first.region_id == region_id) {
            hcache.second.ClearHistograms();
        }
    }
    for (auto& spcache : _spectral_cache) {
        if (spcache.first.region_id == region_id) {
            spcache.second.ClearProfiles();
        }
    }
    for (auto& stcache : _stats_cache) {
        if (stcache.first.region_id == region_id) {
            stcache.second.ClearStats();
        }
    }
}

// ********************************************************************
// Region data stream helpers

bool RegionHandler::RegionFileIdsValid(int region_id, int file_id) {
    // Check error conditions and preconditions
    if (((region_id < 0) && (file_id < 0)) || (region_id == 0)) { // not allowed
        return false;
    }
    if (!RegionSet(region_id)) { // no Region(s) for this id or Region is closing
        return false;
    }
    if (!FrameSet(file_id)) { // no Frame(s) for this id or Frame is closing
        return false;
    }
    return true;
}

bool RegionHandler::ApplyRegionToFile(
    int region_id, int file_id, const ChannelRange& chan_range, int stokes, casacore::ImageRegion& region) {
    // Returns image region for given region (region_id) applied to given image (file_id)
    if (!RegionSet(region_id) || !FrameSet(file_id)) {
        return false;
    }

    try {
        casacore::CoordinateSystem coord_sys(_frames.at(file_id)->CoordinateSystem());
        casacore::IPosition image_shape(_frames.at(file_id)->ImageShape());
        casacore::LCRegion* applied_region = _regions.at(region_id)->GetImageRegion(file_id, coord_sys, image_shape);
        if (applied_region == nullptr) {
            return false;
        }

        // Create LCRegion with chan range and stokes using a slicer
        casacore::Slicer chan_stokes_slicer = _frames.at(file_id)->GetImageSlicer(chan_range, stokes);
        casacore::LCBox chan_stokes_box(chan_stokes_slicer, image_shape);

        // Intersection combines applied_region limits in xy axes and chan_stokes_box limits in channel and stokes axes
        casacore::LCIntersection final_region(*applied_region, chan_stokes_box);

        // Return ImageRegion
        region = casacore::ImageRegion(final_region);
        return true;
    } catch (const casacore::AipsError& err) {
        std::cerr << "Error applying region " << region_id << " to file " << file_id << ": " << err.getMesg() << std::endl;
    } catch (std::out_of_range& range_error) {
        std::cerr << "Cannot apply region " << region_id << " to closed file " << file_id << std::endl;
    }

    return false;
}

// ********************************************************************
// Fill data stream messages:
// These always use a callback since there may be multiple region/file requirements
// region_id > 0 file_id >= 0   update data for specified region/file
// region_id > 0 file_id < 0    update data for all files in region's requirements (region changed)
// region_id < 0 file_id >= 0   update data for all regions with file_id (chan/stokes changed)
// region_id < 0 file_id < 0    not allowed (all regions for all files?)
// region_id = 0                not allowed (cursor region handled by Frame)

// ***** Fill histogram *****

bool RegionHandler::FillRegionHistogramData(std::function<void(CARTA::RegionHistogramData histogram_data)> cb, int region_id, int file_id) {
    // Fill histogram data for given region and file
    if (!RegionFileIdsValid(region_id, file_id)) {
        return false;
    }

    bool message_filled(false);
    if (region_id > 0) {
        // Fill histograms for specific region with file_id requirement (specific file_id or all files)
        std::unordered_map<ConfigId, RegionHistogramConfig, ConfigIdHash> region_configs = _histogram_req;
        for (auto& region_config : region_configs) {
            if ((region_config.first.region_id == region_id) && ((region_config.first.file_id == file_id) || (file_id == ALL_FILES))) {
                if (region_config.second.configs.empty()) { // no requirements
                    continue;
                }
                int config_file_id = region_config.first.file_id;
                if (!RegionFileIdsValid(region_id, config_file_id)) { // check specific ids
                    continue;
                }

                // return histograms for this requirement
                CARTA::RegionHistogramData histogram_message;
                std::vector<HistogramConfig> histogram_configs = region_config.second.configs;
                if (GetRegionHistogramData(region_id, config_file_id, histogram_configs, histogram_message)) {
                    cb(histogram_message); // send data
                    message_filled = true;
                }
            }
        }
    } else {
        // (region_id < 0) Fill histograms for all regions with specific file_id requirement
        std::unordered_map<ConfigId, RegionHistogramConfig, ConfigIdHash> region_configs = _histogram_req;
        for (auto& region_config : region_configs) {
            if (region_config.first.file_id == file_id) {
                if (region_config.second.configs.empty()) { // requirements removed
                    continue;
                }
                int config_region_id(region_config.first.region_id);
                if (!RegionFileIdsValid(config_region_id, file_id)) { // check specific ids
                    continue;
                }

                // return histograms for this requirement
                std::vector<HistogramConfig> histogram_configs = region_config.second.configs;
                CARTA::RegionHistogramData histogram_message;
                if (GetRegionHistogramData(config_region_id, file_id, histogram_configs, histogram_message)) {
                    cb(histogram_message); // send data
                    message_filled = true;
                }
            }
        }
    }
    return message_filled;
}

bool RegionHandler::GetRegionHistogramData(
    int region_id, int file_id, std::vector<HistogramConfig>& configs, CARTA::RegionHistogramData& histogram_message) {
    // Fill stats message for given region, file
    auto t_start_region_histogram = std::chrono::high_resolution_clock::now();

    int stokes(_frames.at(file_id)->CurrentStokes());
    int channel(_frames.at(file_id)->CurrentChannel());

    // Set histogram fields
    histogram_message.set_file_id(file_id);
    histogram_message.set_region_id(region_id);
    histogram_message.set_stokes(stokes);
    histogram_message.set_progress(HISTOGRAM_COMPLETE); // only cube histograms have partial results

    // Get image region
    ChannelRange chan_range(channel);
    casacore::ImageRegion region;
    if (!ApplyRegionToFile(region_id, file_id, chan_range, stokes, region)) {
        // region outside image, send default histogram
        auto default_histogram = histogram_message.add_histograms();
        default_histogram->set_channel(channel);
        default_histogram->set_num_bins(1);
        default_histogram->set_bin_width(0.0);
        default_histogram->set_first_bin_center(0.0);
        std::vector<float> histogram_bins(1, 0.0);
        *default_histogram->mutable_bins() = {histogram_bins.begin(), histogram_bins.end()};
        float float_nan = std::numeric_limits<float>::quiet_NaN();
        default_histogram->set_mean(float_nan);
        default_histogram->set_std_dev(float_nan);
        return true;
    }

    // Flags for calculations
    bool have_region_data(false);
    bool have_basic_stats(false);

    // Reuse data and stats for each histogram; results depend on num_bins
    std::vector<float> data;
    BasicStats<float> stats;

    // Key for cache
    CacheId cache_id = CacheId(file_id, region_id, stokes, channel);

    // Calculate histogram for each requirement
    for (auto& hist_config : configs) {
        // check for cancel
        if (!RegionFileIdsValid(region_id, file_id)) {
            return false;
        }

        // number of bins may be set or calculated
        int num_bins(hist_config.num_bins);
        if (num_bins == AUTO_BIN_SIZE) {
            casacore::IPosition region_shape = _frames.at(file_id)->GetRegionShape(region);
            num_bins = int(std::max(sqrt(region_shape(0) * region_shape(1)), 2.0));
        }

        // check cache
        if (_histogram_cache.count(cache_id)) {
            have_basic_stats = _histogram_cache[cache_id].GetBasicStats(stats);
            if (have_basic_stats) {
                carta::HistogramResults results;
                if (_histogram_cache[cache_id].GetHistogram(num_bins, results)) {
                    auto histogram = histogram_message.add_histograms();
                    histogram->set_channel(channel);
                    FillHistogramFromResults(histogram, stats, results);
                    continue;
                }
            }
        }

        // Calculate stats and/or histograms, not in cache
        // Get data in region
        if (!have_region_data) {
            have_region_data = _frames.at(file_id)->GetRegionData(region, data);
            if (!have_region_data) {
                return false;
            }
        }

        // Calculate and cache stats
        if (!have_basic_stats) {
            CalcBasicStats(data, stats);
            _histogram_cache[cache_id].SetBasicStats(stats);
            have_basic_stats = true;
        }

        // Calculate and cache histogram for number of bins
        HistogramResults results;
        CalcHistogram(num_bins, stats, data, results);
        _histogram_cache[cache_id].SetHistogram(num_bins, results);

        // Complete Histogram submessage
        auto histogram = histogram_message.add_histograms();
        histogram->set_channel(channel);
        FillHistogramFromResults(histogram, stats, results);
    }

    if (_verbose) {
        auto t_end_region_histogram = std::chrono::high_resolution_clock::now();
        auto dt_region_histogram =
            std::chrono::duration_cast<std::chrono::microseconds>(t_end_region_histogram - t_start_region_histogram).count();
        fmt::print("Fill region histogram in {} ms at {} MPix/s\n", dt_region_histogram, (float)stats.num_pixels / dt_region_histogram);
    }

    return true;
}

// ***** Fill spectral profile *****

bool RegionHandler::FillSpectralProfileData(
    std::function<void(CARTA::SpectralProfileData profile_data)> cb, int region_id, int file_id, bool stokes_changed) {
    // Fill spectral profiles for given region and file
    if (!RegionFileIdsValid(region_id, file_id)) {
        return false;
    }

    std::unordered_map<ConfigId, RegionSpectralConfig, ConfigIdHash> region_configs = _spectral_req;

    bool profile_ok(false);
    if (region_id > 0) {
        // Fill spectral profile for specific region with file_id requirement (specific file_id or all files)
        for (auto& region_config : region_configs) {
            if ((region_config.first.region_id == region_id) && ((region_config.first.file_id == file_id) || (file_id == ALL_FILES))) {
                if (region_config.second.configs.empty()) { // no requirements
                    continue;
                }
                int config_file_id(region_config.first.file_id);
                if (!RegionFileIdsValid(region_id, config_file_id)) { // check specific ids
                    continue;
                }

                for (auto& spectral_config : region_config.second.configs) {
                    // Do not update for specific stokes when stokes changes
                    int config_stokes(spectral_config.stokes);
                    std::vector<CARTA::StatsType> required_stats;
                    if (stokes_changed) {
                        if (config_stokes != CURRENT_STOKES) {
                            continue;
                        }
                        required_stats = spectral_config.all_stats;
                    } else {
                        required_stats = spectral_config.new_stats;
                    }
                    if (required_stats.empty()) { // no requirements or no new stats to send
                        profile_ok = true;
                        continue;
                    }

                    int stokes_index = (config_stokes == CURRENT_STOKES ? _frames.at(config_file_id)->CurrentStokes() : config_stokes);

                    // Return spectral profile for this requirement
                    profile_ok = GetRegionSpectralData(region_id, config_file_id, config_stokes, stokes_index, required_stats,
                        [&](std::map<CARTA::StatsType, std::vector<double>> results, float progress) {
                            CARTA::SpectralProfileData profile_message;
                            profile_message.set_file_id(config_file_id);
                            profile_message.set_region_id(region_id);
                            profile_message.set_stokes(stokes_index);
                            profile_message.set_progress(progress);
                            FillSpectralProfileDataMessage(profile_message, spectral_config.coordinate, required_stats, results);
                            cb(profile_message); // send (partial profile) data
                        });
                }
            }
        }
    } else {
        // (region_id < 0) Fill spectral profile for all regions with specific file_id requirement
        for (auto& region_config : region_configs) {
            if (region_config.first.file_id == file_id) {
                if (region_config.second.configs.empty()) { // no requirements
                    continue;
                }
                int config_region_id(region_config.first.region_id);
                if (!RegionFileIdsValid(config_region_id, file_id)) { // check specific ids
                    continue;
                }

                for (auto& spectral_config : region_config.second.configs) {
                    // Do not update for specific stokes when stokes changes
                    int config_stokes(spectral_config.stokes);
                    std::vector<CARTA::StatsType> required_stats;

                    if (stokes_changed) {
                        if (config_stokes != CURRENT_STOKES) {
                            continue;
                        }
                        required_stats = spectral_config.all_stats;
                    } else {
                        required_stats = spectral_config.new_stats;
                    }
                    if (required_stats.empty()) { // no requirements or no new stats to send
                        profile_ok = true;
                        continue;
                    }

                    int stokes_index = (config_stokes == CURRENT_STOKES ? _frames.at(file_id)->CurrentStokes() : config_stokes);

                    // Return profile(s) for this requirement; each stats type is its own spectral data message
                    profile_ok = GetRegionSpectralData(config_region_id, file_id, config_stokes, stokes_index, required_stats,
                        [&](std::map<CARTA::StatsType, std::vector<double>> results, float progress) {
                            CARTA::SpectralProfileData profile_message;
                            profile_message.set_file_id(file_id);
                            profile_message.set_region_id(config_region_id);
                            profile_message.set_stokes(stokes_index);
                            profile_message.set_progress(progress);
                            FillSpectralProfileDataMessage(profile_message, spectral_config.coordinate, required_stats, results);
                            cb(profile_message); // send (partial profile) data
                        });
                }
            }
        }
    }
    return profile_ok;
}

bool RegionHandler::GetRegionSpectralData(int region_id, int file_id, int config_stokes, int stokes_index,
    std::vector<CARTA::StatsType>& required_stats,
    const std::function<void(std::map<CARTA::StatsType, std::vector<double>>, float)>& partial_results_callback) {
    // Fill spectral profile message for given region, file, and requirement
    if (!RegionFileIdsValid(region_id, file_id)) {
        return false;
    }
    // Check cancel
    if (!HasSpectralRequirements(region_id, file_id, config_stokes)) {
        return false;
    }

    auto t_start_spectral_profile = std::chrono::high_resolution_clock::now();
    _frames.at(file_id)->IncreaseZProfileCount();
    _regions.at(region_id)->IncreaseZProfileCount();

    // Initialize results to NaN, progress to complete
    size_t profile_size = _frames.at(file_id)->NumChannels(); // total number of channels
    std::vector<double> init_spectral(profile_size, std::numeric_limits<double>::quiet_NaN());

    std::map<CARTA::StatsType, std::vector<double>> results;
    for (const auto& stat : required_stats) {
        results[stat] = init_spectral;
    }
    float progress(0.0);

    // Initialize cache results for all spectral stats
    std::map<CARTA::StatsType, std::vector<double>> cache_results;
    for (const auto& stat : _spectral_stats) {
        cache_results[stat] = init_spectral;
    }

    // Get initial stokes to cancel profile if it changes
    bool use_current_stokes(config_stokes == CURRENT_STOKES);
    int initial_stokes(stokes_index);

    // Get initial region state to cancel profile if it changes
    RegionState initial_state = _regions.at(region_id)->GetRegionState();

    // Get region with all channels
    ChannelRange chan_range(ALL_CHANNELS);
    casacore::ImageRegion region;
    if (!ApplyRegionToFile(region_id, file_id, chan_range, initial_stokes, region)) {
        progress = PROFILE_COMPLETE;
        partial_results_callback(results, progress); // region outside image, send NaNs
        _frames.at(file_id)->DecreaseZProfileCount();
        _regions.at(region_id)->DecreaseZProfileCount();
        return true;
    }

    // Use loader swizzled data for efficiency
    if (_frames.at(file_id)->UseLoaderSpectralData(region)) {
        // Get origin
        casacore::CoordinateSystem csys = _frames.at(file_id)->CoordinateSystem();
        casacore::IPosition shape = _frames.at(file_id)->ImageShape();
        casacore::LatticeRegion lattice_region = region.toLatticeRegion(csys, shape);
        casacore::IPosition origin(lattice_region.slicer().start());

        // Get mask
        casacore::Array<casacore::Bool> mask;
        if (_frames.at(file_id)->GetRegionMask(region, mask)) {
            // Get partial profiles until complete (do once if cached)
            while (progress < PROFILE_COMPLETE) {
                // Cancel if region, current stokes, or spectral requirements changed
                if (!RegionFileIdsValid(region_id, file_id)) {
                    _frames.at(file_id)->DecreaseZProfileCount();
                    _regions.at(region_id)->DecreaseZProfileCount();
                    return false;
                }

                if (_regions.at(region_id)->GetRegionState() != initial_state) {
                    _frames.at(file_id)->DecreaseZProfileCount();
                    _regions.at(region_id)->DecreaseZProfileCount();
                    return false;
                }
                if (use_current_stokes && (initial_stokes != _frames.at(file_id)->CurrentStokes())) {
                    _frames.at(file_id)->DecreaseZProfileCount();
                    _regions.at(region_id)->DecreaseZProfileCount();
                    return false;
                }
                if (!HasSpectralRequirements(region_id, file_id, config_stokes)) {
                    _frames.at(file_id)->DecreaseZProfileCount();
                    _regions.at(region_id)->DecreaseZProfileCount();
                    return false;
                }

                // Get partial profile
                std::map<CARTA::StatsType, std::vector<double>> partial_profiles;
                if (_frames.at(file_id)->GetLoaderSpectralData(region_id, initial_stokes, mask, origin, partial_profiles, progress)) {
                    // Copy partial profile to results
                    for (const auto& profile : partial_profiles) {
                        auto stats_type = profile.first;
                        if (results.count(stats_type)) {
                            results[stats_type] = profile.second;
                        }
                    }
                    partial_results_callback(results, progress);
                } else {
                    _frames.at(file_id)->DecreaseZProfileCount();
                    _regions.at(region_id)->DecreaseZProfileCount();
                    return false;
                }
            }

            if (_verbose) {
                auto t_end_spectral_profile = std::chrono::high_resolution_clock::now();
                auto dt_spectral_profile =
                    std::chrono::duration_cast<std::chrono::milliseconds>(t_end_spectral_profile - t_start_spectral_profile).count();
                fmt::print("Fill spectral profile in {} ms\n", dt_spectral_profile);
            }

            _frames.at(file_id)->DecreaseZProfileCount();
            _regions.at(region_id)->DecreaseZProfileCount();
            return true;
        }
    } // end loader swizzled data

    // Check cache
    CacheId cache_id(file_id, region_id, initial_stokes);
    if (_spectral_cache.count(cache_id) && !_spectral_cache[cache_id].profiles.empty()) {
        // Copy profiles to results
        for (auto& result : results) {
            auto stats_type = result.first;
            std::vector<double> profile;
            if (_spectral_cache[cache_id].GetProfile(stats_type, profile)) {
                results[stats_type] = profile;
            }
        }
        progress = PROFILE_COMPLETE;
        partial_results_callback(results, progress);

        _frames.at(file_id)->DecreaseZProfileCount();
        _regions.at(region_id)->DecreaseZProfileCount();
        return true;
    }

    // Calculate and cache profiles
    size_t start_channel(0), count(0), end_channel(0);
    int delta_channels = INIT_DELTA_CHANNEL; // the increment of channels for each step
    int dt_target = TARGET_DELTA_TIME;       // the target time elapse for each step, in the unit of milliseconds
    auto t_partial_profile_start = std::chrono::high_resolution_clock::now();

    // get stats data
    while (start_channel < profile_size) {
        // start the timer
        auto t_start = std::chrono::high_resolution_clock::now();

        // Cancel if region or frame is closing
        if (!RegionFileIdsValid(region_id, file_id)) {
            _frames.at(file_id)->DecreaseZProfileCount();
            _regions.at(region_id)->DecreaseZProfileCount();
            return false;
        }
        // Cancel if region, current stokes, or spectral requirements changed
        if (_regions.at(region_id)->GetRegionState() != initial_state) {
            _frames.at(file_id)->DecreaseZProfileCount();
            _regions.at(region_id)->DecreaseZProfileCount();
            return false;
        }
        if (use_current_stokes && (initial_stokes != _frames.at(file_id)->CurrentStokes())) {
            _frames.at(file_id)->DecreaseZProfileCount();
            _regions.at(region_id)->DecreaseZProfileCount();
            return false;
        }
        if (!HasSpectralRequirements(region_id, file_id, config_stokes)) {
            _frames.at(file_id)->DecreaseZProfileCount();
            _regions.at(region_id)->DecreaseZProfileCount();
            return false;
        }

        end_channel = (start_channel + delta_channels > profile_size ? profile_size - 1 : start_channel + delta_channels - 1);
        count = end_channel - start_channel + 1;

        // Get region for channel range and stokes
        ChannelRange chan_range(start_channel, end_channel);
        casacore::ImageRegion region;
        if (!ApplyRegionToFile(region_id, file_id, chan_range, initial_stokes, region)) {
            _frames.at(file_id)->DecreaseZProfileCount();
            _regions.at(region_id)->DecreaseZProfileCount();
            return false;
        }

        // Get per-channel stats data for region for all spectral stats (for cache)
        bool per_channel(true);
        std::map<CARTA::StatsType, std::vector<double>> partial_profiles;
        if (!_frames.at(file_id)->GetRegionStats(region, _spectral_stats, per_channel, partial_profiles)) {
            _frames.at(file_id)->DecreaseZProfileCount();
            _regions.at(region_id)->DecreaseZProfileCount();
            return false;
        }

        // Copy partial profile to results and cache_results (all stats)
        for (const auto& profile : partial_profiles) {
            auto stats_type = profile.first;
            const std::vector<double>& stats_data = profile.second;
            if (results.count(stats_type)) {
                memcpy(&results[stats_type][start_channel], &stats_data[0], stats_data.size() * sizeof(double));
            }
            memcpy(&cache_results[stats_type][start_channel], &stats_data[0], stats_data.size() * sizeof(double));
        }

        start_channel += count;
        progress = (float)start_channel / profile_size;

        // get the time elapse for this step
        auto t_end = std::chrono::high_resolution_clock::now();
        auto dt = std::chrono::duration<double, std::milli>(t_end - t_start).count();
        auto dt_partial_profile = std::chrono::duration<double, std::milli>(t_end - t_partial_profile_start).count();

        // adjust the increment of channels according to the time elapse
        delta_channels *= dt_target / dt;
        if (delta_channels < 1) {
            delta_channels = 1;
        }
        if (delta_channels > profile_size) {
            delta_channels = profile_size;
        }

        // send partial result by the callback function
        if (dt_partial_profile > TARGET_PARTIAL_REGION_TIME || progress >= PROFILE_COMPLETE) {
            t_partial_profile_start = std::chrono::high_resolution_clock::now();
            partial_results_callback(results, progress);
            if (progress >= PROFILE_COMPLETE) {
                // cache results for all stats types
                _spectral_cache[cache_id] = SpectralCache(cache_results);
            }
        }
    }

    if (_verbose) {
        auto t_end_spectral_profile = std::chrono::high_resolution_clock::now();
        auto dt_spectral_profile =
            std::chrono::duration_cast<std::chrono::milliseconds>(t_end_spectral_profile - t_start_spectral_profile).count();
        fmt::print("Fill spectral profile in {} ms\n", dt_spectral_profile);
    }

    _frames.at(file_id)->DecreaseZProfileCount();
    _regions.at(region_id)->DecreaseZProfileCount();
    return true;
}

// ***** Fill stats data *****

bool RegionHandler::FillRegionStatsData(std::function<void(CARTA::RegionStatsData stats_data)> cb, int region_id, int file_id) {
    // Fill stats data for given region and file
    if (!RegionFileIdsValid(region_id, file_id)) {
        return false;
    }

    bool message_filled(false);
    if (region_id > 0) {
        // Fill stats data for specific region with file_id requirement (specific file_id or all files)
        std::unordered_map<ConfigId, RegionStatsConfig, ConfigIdHash> region_configs = _stats_req;
        for (auto& region_config : region_configs) {
            if ((region_config.first.region_id == region_id) && ((region_config.first.file_id == file_id) || (file_id == ALL_FILES))) {
                if (region_config.second.stats_types.empty()) { // no requirements
                    continue;
                }
                int config_file_id(region_config.first.file_id);
                if (!RegionFileIdsValid(region_id, config_file_id)) { // check specific ids
                    continue;
                }

                // return stats for this requirement
                CARTA::RegionStatsData stats_message;
                if (GetRegionStatsData(region_id, config_file_id, region_config.second.stats_types, stats_message)) {
                    cb(stats_message); // send data
                    message_filled = true;
                }
            }
        }
    } else {
        // (region_id < 0) Fill stats data for all regions with specific file_id requirement
        int channel(_frames.at(file_id)->CurrentChannel());
        int stokes(_frames.at(file_id)->CurrentStokes());

        // Find requirements with file_id
        std::unordered_map<ConfigId, RegionStatsConfig, ConfigIdHash> region_configs = _stats_req;
        for (auto& region_config : region_configs) {
            if (region_config.first.file_id == file_id) {
                if (region_config.second.stats_types.empty()) { // no requirements
                    continue;
                }
                int config_region_id(region_config.first.region_id);
                if (!RegionFileIdsValid(config_region_id, file_id)) { // check specific ids
                    continue;
                }

                // return stats for this requirement
                CARTA::RegionStatsData stats_message;
                if (GetRegionStatsData(config_region_id, file_id, region_config.second.stats_types, stats_message)) {
                    cb(stats_message); // send data
                    message_filled = true;
                }
            }
        }
    }
    return message_filled;
}

bool RegionHandler::GetRegionStatsData(
    int region_id, int file_id, std::vector<CARTA::StatsType>& required_stats, CARTA::RegionStatsData& stats_message) {
    // Fill stats message for given region, file
    auto t_start_region_stats = std::chrono::high_resolution_clock::now();

    int channel(_frames.at(file_id)->CurrentChannel());
    int stokes(_frames.at(file_id)->CurrentStokes());

    // Start filling message
    stats_message.set_file_id(file_id);
    stats_message.set_region_id(region_id);
    stats_message.set_channel(channel);
    stats_message.set_stokes(stokes);

    // Check cache
    CacheId cache_id = CacheId(file_id, region_id, stokes, channel);
    if (_stats_cache.count(cache_id)) {
        std::map<CARTA::StatsType, double> stats_results;
        if (_stats_cache[cache_id].GetStats(stats_results)) {
            FillStatisticsValuesFromMap(stats_message, required_stats, stats_results);
            return true;
        }
    }

    // Get region
    ChannelRange chan_range(channel);
    casacore::ImageRegion region;
    if (!ApplyRegionToFile(region_id, file_id, chan_range, stokes, region)) {
        // region outside image: NaN results
        std::map<CARTA::StatsType, double> stats_results;
        for (const auto& carta_stat : required_stats) {
            if (carta_stat == CARTA::StatsType::NumPixels) {
                stats_results[carta_stat] = 0.0;
            } else {
                stats_results[carta_stat] = std::numeric_limits<double>::quiet_NaN();
            }
        }
        FillStatisticsValuesFromMap(stats_message, required_stats, stats_results);
        // cache results
        _stats_cache[cache_id] = StatsCache(stats_results);
        return true;
    }

    // calculate stats
    bool per_channel(false);
    std::map<CARTA::StatsType, std::vector<double>> stats_map;
    if (_frames.at(file_id)->GetRegionStats(region, required_stats, per_channel, stats_map)) {
        // convert vector to single value in map
        std::map<CARTA::StatsType, double> stats_results;
        for (auto& value : stats_map) {
            stats_results[value.first] = value.second[0];
        }

        // add values to message
        FillStatisticsValuesFromMap(stats_message, required_stats, stats_results);
        // cache results
        _stats_cache[cache_id] = StatsCache(stats_results);

        if (_verbose) {
            auto t_end_region_stats = std::chrono::high_resolution_clock::now();
            auto dt_region_stats = std::chrono::duration_cast<std::chrono::milliseconds>(t_end_region_stats - t_start_region_stats).count();
            fmt::print("Fill region stats in {} ms\n", dt_region_stats);
        }
        return true;
    }

    return false;
}
