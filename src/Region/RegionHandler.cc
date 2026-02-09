/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// RegionHandler.cc: handle requirements and data streams for regions

#include "RegionHandler.h"

#include <chrono>

#include <casacore/casa/math.h>

#include "ImageData/FileLoader.h"
#include "ImageRegion.h"
#include "ImageStats/StatsCalculator.h"
#include "Logger/Logger.h"
#include "RegionAnalysis/LineBoxRegions.h"
#include "RegionImportExport/RegionImportExport.h"
#include "Timer/Timer.h"
#include "Util/File.h"
#include "Util/Image.h"
#include "Util/Nan.h"

#define LINE_PROFILE_PROGRESS_INTERVAL 500

namespace carta {

RegionHandler::~RegionHandler() {
    RemoveRegion(ALL_REGIONS);
    RemoveFrame(ALL_FILES);
}

// ********************************************************************
// Region handling

int RegionHandler::GetNextRegionId() {
    // Returns maximum id + 1; start at 1 if no regions set
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

int RegionHandler::GetNextTemporaryRegionId() {
    int min_id(TEMP_REGION_ID);
    if (!_regions.empty()) {
        for (auto& region : _regions) {
            if (region.first < min_id) {
                min_id = region.first;
            }
        }
    }
    return min_id - 1;
}

bool RegionHandler::SetRegion(int& region_id, RegionState& region_state, std::shared_ptr<casacore::CoordinateSystem> csys) {
    // Set region params for region id; if id < 0, create new id
    // CoordinateSystem will be owned by Region
    bool valid_region(false);

    // Check for id > 0 so do not update temp region
    if ((region_id > 0) && RegionSet(region_id)) {
        auto region = GetRegion(region_id);
        region->UpdateRegion(region_state);
        valid_region = region->IsValid();

        if (region->RegionChanged()) {
            UpdateNewSpectralRequirements(region_id); // set all req "new"
            ClearRegionCache(region_id);
        }
    } else {
        std::unique_lock<std::mutex> region_lock(_region_mutex);
        if (region_id == NEW_REGION_ID) {
            // new region, assign (positive) id
            region_id = GetNextRegionId();
        } else if (region_id == TEMP_REGION_ID) {
            // new region, assign (negative) id
            region_id = GetNextTemporaryRegionId();
        }

        auto region = std::shared_ptr<Region>(new Region(region_state, csys));
        if (region && region->IsValid()) {
            _regions[region_id] = std::move(region);
            valid_region = true;
        }
    }
    return valid_region;
}

void RegionHandler::RemoveRegion(int region_id) {
    // Call destructor and erase from map
    if (!RegionSet(region_id)) {
        return;
    }

    // Disconnect region(s)
    if (region_id == ALL_REGIONS) {
        for (auto& region : _regions) {
            region.second->WaitForTaskCancellation();
        }
    } else {
        _regions.at(region_id)->WaitForTaskCancellation();
    }

    // Erase region(s)
    std::unique_lock<std::mutex> region_lock(_region_mutex);
    if (region_id == ALL_REGIONS) {
        _regions.clear();
    } else {
        _regions.erase(region_id);
    }
    region_lock.unlock();

    RemoveRegionRequirementsCache(region_id);
}

std::shared_ptr<Region> RegionHandler::GetRegion(int region_id) {
    if (RegionSet(region_id)) {
        std::lock_guard<std::mutex> region_guard(_region_mutex);
        return _regions.at(region_id);
    } else {
        return std::shared_ptr<Region>();
    }
}

bool RegionHandler::RegionSet(int region_id, bool check_annotation) {
    // Check whether a particular region is set or any regions are set
    bool region_set(false);
    std::lock_guard<std::mutex> region_guard(_region_mutex);
    if (region_id == ALL_REGIONS) {
        region_set = _regions.size() > 0;
    } else {
        region_set = (_regions.find(region_id) != _regions.end()) && _regions.at(region_id)->IsConnected();
        if (region_set && check_annotation) {
            region_set = !_regions.at(region_id)->IsAnnotation();
        }
    }
    return region_set;
}

void RegionHandler::ImportRegion(int file_id, std::shared_ptr<Frame> frame, CARTA::FileType region_file_type,
    const std::string& region_file, bool file_is_filename, CARTA::ImportRegionAck& import_ack) {
    // Set regions from region file
    auto csys = frame->CoordinateSystem();
    std::unique_ptr<RegionImporter> importer(nullptr);

    try {
        importer = GetRegionImporter(region_file_type, csys, file_id, region_file, file_is_filename);
    } catch (const casacore::AipsError& err) {
        import_ack.set_success(false);
        import_ack.set_message("Region import failed: " + err.getMesg());
        return;
    }

    if (!importer) {
        import_ack.set_success(false);
        import_ack.set_message("Region importer failed.");
        return;
    }

    // Get regions and error message from importer
    std::string error;
    auto imported_regions = importer->GetRegions(error);
    import_ack.set_message(error);
    if (imported_regions.empty()) {
        import_ack.set_success(false);
        return;
    }

    // Set frame for region reference file
    _frames[file_id] = frame;

    // Set Region from RegionProperties; if successful, add RegionInfo to ack message
    auto region_info_map = import_ack.mutable_regions();
    auto region_style_map = import_ack.mutable_region_styles();
    int region_id = GetNextRegionId();
    bool success(false);

    for (auto& region_properties : imported_regions) {
        auto region_state = region_properties.state;
        auto region_style = region_properties.style;
        auto region = std::shared_ptr<Region>(new Region(region_state, csys));

        if (region && region->IsValid()) {
            std::unique_lock<std::mutex> region_lock(_region_mutex);
            _regions[region_id] = std::move(region);
            region_lock.unlock();

            CARTA::RegionInfo region_info;
            region_info.set_region_type(region_state.type);
            *region_info.mutable_control_points() = {region_state.control_points.begin(), region_state.control_points.end()};
            region_info.set_rotation(region_state.rotation);
            (*region_info_map)[region_id] = region_info;
            (*region_style_map)[region_id++] = region_style;
            success = true; // if any regions were set
        }
    }
    import_ack.set_success(success);
}

void RegionHandler::ExportRegion(int file_id, std::shared_ptr<Frame> frame, CARTA::FileType region_file_type,
    CARTA::CoordinateType coord_type, std::map<int, CARTA::RegionStyle>& region_styles, std::string& filename, bool overwrite,
    CARTA::ExportRegionAck& export_ack) {
    // Export regions to given filename, or return export file contents in ack
    // Check if any regions to export
    if (region_styles.empty()) {
        export_ack.set_success(false);
        export_ack.set_message("Export region failed: no regions requested.");
        return;
    }

    auto output_csys = frame->CoordinateSystem();
    auto output_shape = frame->ImageShape();
    auto stokes_axis = frame->StokesAxis();

    bool export_pixel_coords(coord_type == CARTA::CoordinateType::PIXEL);
    if (!export_pixel_coords && !output_csys->hasDirectionCoordinate()) {
        // Export fails, cannot convert to world coordinates
        export_ack.set_success(false);
        export_ack.set_message("Cannot export regions in world coordinates for linear coordinate system.");
        return;
    }

    auto exporter = GetRegionExporter(region_file_type, output_csys, output_shape, stokes_axis, export_pixel_coords);
    if (!exporter->CanExportToFile(filename, overwrite, export_ack)) {
        return;
    }

    std::string export_errors; // for ack message
    for (auto& region_id_style : region_styles) {
        auto region_id = region_id_style.first;
        if (RegionSet(region_id)) {
            auto region = GetRegion(region_id);
            auto region_style = region_id_style.second;
            if (!exporter->AddRegion(file_id, region, region_style, export_pixel_coords)) {
                std::string region_error = fmt::format("Export region {} in image {} failed.\n", region_id, file_id);
                export_errors.append(region_error);
            }
        } else {
            std::string region_error = fmt::format("Region {} not found for export.\n", region_id);
            export_errors.append(region_error);
        }
    }

    // Export regions to file or contents, and complete ack message.
    exporter->ExportRegions(filename, export_errors, export_ack);
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
        StopPvCalc(file_id);
        _frames.erase(file_id);
        RemoveFileRequirementsCache(file_id);
    }
}

// ********************************************************************
// Region requirements handling

bool RegionHandler::SetHistogramRequirements(
    int region_id, int file_id, std::shared_ptr<Frame> frame, const std::vector<CARTA::HistogramConfig>& configs) {
    // Set histogram configurations for closed region
    if (configs.empty() && !RegionSet(region_id)) {
        // Frontend clears requirements after region removed, prevent error in log by returning true.
        return true;
    }

    if (!RegionSet(region_id, true)) {
        spdlog::error("Histogram requirements failed: no region with id {} or is annotation only", region_id);
        return false;
    }

    if (!IsClosedRegion(region_id)) {
        spdlog::debug("Histogram requirements not valid for region {} type", region_id);
        return false;
    }

    // Save frame pointer
    _frames[file_id] = frame;

    if (_region_histograms.find(region_id) == _region_histograms.end()) {
        _region_histograms[region_id] = std::make_unique<RegionHistogram>(region_id, file_id, configs);
    } else {
        _region_histograms[region_id]->SetConfigurations(file_id, configs);
    }

    return true;
}

bool RegionHandler::SetSpatialRequirements(
    int region_id, int file_id, std::shared_ptr<Frame> frame, const std::vector<CARTA::SetSpatialRequirements_SpatialConfig>& configs) {
    // Set spatial requirements for point or line

    if (configs.empty() && !RegionSet(region_id)) {
        // Frontend clears requirements after region removed, prevent error in log by returning true.
        return true;
    }

    if (!RegionSet(region_id, true)) {
        spdlog::error("Spatial requirements failed: no region with id {} or is annotation only", region_id);
        return false;
    }

    if (!IsPointRegion(region_id) && !IsLineRegion(region_id)) {
        spdlog::debug("Spatial requirements not valid for region {} type", region_id);
        return false;
    }

    // Save frame pointer
    _frames[file_id] = frame;

    std::unique_lock<std::mutex> ulock(_spatial_mutex);
    if (_region_spatial_profiles.find(region_id) == _region_spatial_profiles.end()) {
        _region_spatial_profiles[region_id] = std::make_shared<RegionSpatialProfile>(region_id, file_id, configs);
    } else {
        _region_spatial_profiles[region_id]->SetConfigurations(file_id, configs);
    }

    return true;
}

bool RegionHandler::SetSpectralRequirements(int region_id, int file_id, std::shared_ptr<Frame> frame,
    const std::vector<CARTA::SetSpectralRequirements_SpectralConfig>& spectral_profiles) {
    // Set spectral profile requirements for point or closed region

    if (spectral_profiles.empty() && !RegionSet(region_id)) {
        // Frontend clears requirements after region removed, prevent error in log by returning true.
        return true;
    }

    if (!RegionSet(region_id, true)) {
        spdlog::error("Spectral requirements failed: no region with id {} or is annotation only", region_id);
        return false;
    }

    if (!IsPointRegion(region_id) && !IsClosedRegion(region_id)) {
        spdlog::debug("Spectral requirements not valid for region {} type", region_id);
        return false;
    }

    // Save frame pointer
    _frames[file_id] = frame;

    // Clear all requirements for this file/region
    ConfigId config_id(file_id, region_id);
    if (spectral_profiles.empty()) {
        if (_spectral_req.count(config_id)) {
            std::unique_lock<std::mutex> ulock(_spectral_mutex);
            _spectral_req[config_id].configs.clear();
            ulock.unlock();
        }
        return true;
    }

    // Create RegionSpectralConfig for new requirements
    int nstokes = frame->NumStokes();
    std::vector<SpectralConfig> new_configs;
    for (auto& profile : spectral_profiles) {
        // check stokes coordinate
        std::string profile_coordinate(profile.coordinate());
        int stokes_index;
        if (!frame->GetStokesTypeIndex(profile_coordinate, stokes_index)) {
            continue;
        }

        // Create stats vector
        std::vector<CARTA::StatsType> required_stats;
        for (size_t i = 0; i < profile.stats_types_size(); ++i) {
            required_stats.push_back(profile.stats_types(i));
        }

        // Add SpectralConfig to vector
        SpectralConfig spec_config(profile_coordinate, required_stats);
        new_configs.push_back(spec_config);
    }

    if (new_configs.empty()) { // no valid requirements
        return false;
    }

    if (_spectral_req.count(config_id) && !_spectral_req[config_id].configs.empty()) {
        // Diff existing requirements to set new_stats in new_configs
        std::vector<SpectralConfig> current_configs;
        std::unique_lock<std::mutex> ulock(_spectral_mutex);
        current_configs.insert(current_configs.begin(), _spectral_req[config_id].configs.begin(), _spectral_req[config_id].configs.end());
        ulock.unlock();

        // Find matching requirement to set new_stats in new configs
        for (auto& new_config : new_configs) {
            for (auto& current_config : current_configs) {
                if (new_config.coordinate == current_config.coordinate) {
                    // Found current requirement that matches new requirement; determine new stats types
                    std::vector<CARTA::StatsType> new_stats_types;
                    for (auto new_stat : new_config.all_stats) {
                        bool found_stat(false);
                        for (auto current_stat : current_config.all_stats) {
                            if (current_stat == new_stat) {
                                found_stat = true;
                                break;
                            }
                        }
                        if (!found_stat) {
                            new_stats_types.push_back(new_stat);
                        }
                    }
                    new_config.SetNewRequirements(new_stats_types);
                    break;
                }
            }
        }
    }

    // Update region config in spectral req map
    RegionSpectralConfig region_config;
    region_config.configs = new_configs;
    std::unique_lock<std::mutex> ulock(_spectral_mutex);
    _spectral_req[config_id] = region_config;
    ulock.unlock();

    return true;
}

bool RegionHandler::HasSpectralRequirements(
    int region_id, int file_id, const std::string& coordinate, const std::vector<CARTA::StatsType>& required_stats) {
    // Search _spectral_req for given file, region, and stokes; check if _any_ requested stats still valid.
    // Used to check for cancellation.
    ConfigId config_id(file_id, region_id);
    std::vector<SpectralConfig> spectral_configs;
    std::unique_lock<std::mutex> ulock(_spectral_mutex);
    spectral_configs.insert(spectral_configs.begin(), _spectral_req[config_id].configs.begin(), _spectral_req[config_id].configs.end());
    ulock.unlock();

    bool has_stat(false);
    for (auto& config : spectral_configs) {
        if (config.coordinate == coordinate) {
            // Found config, now find stats
            for (auto stat : required_stats) {
                if (config.HasStat(stat)) {
                    has_stat = true;
                    break;
                }
            }
            return has_stat;
        }
    }

    return has_stat;
}

void RegionHandler::UpdateNewSpectralRequirements(int region_id) {
    // Set all requirements "new" when region changes
    std::lock_guard<std::mutex> guard(_spectral_mutex);
    for (auto& req : _spectral_req) {
        if (req.first.region_id == region_id) {
            for (auto& spec_config : req.second.configs) {
                spec_config.SetAllNewStats();
            }
        }
    }
}

bool RegionHandler::SetStatsRequirements(
    int region_id, int file_id, std::shared_ptr<Frame> frame, const std::vector<CARTA::SetStatsRequirements_StatsConfig>& configs) {
    // Set stats data requirements for closed region

    if (configs.empty() && !RegionSet(region_id)) {
        // frontend clears requirements after region removed, prevent error in log
        return true;
    }

    if (!RegionSet(region_id, true)) {
        spdlog::error("Statistics requirements failed: no region with id {} or is annotation only", region_id);
        return false;
    }

    if (!IsClosedRegion(region_id)) {
        spdlog::debug("Statistics requirements not valid for region {} type", region_id);
        return false;
    }

    // Save frame pointer
    _frames[file_id] = frame;

    // Set configurations
    if (_region_statistics.find(region_id) == _region_statistics.end()) {
        _region_statistics[region_id] = std::make_unique<RegionStatistics>(region_id, file_id, configs);
    } else {
        _region_statistics[region_id]->SetConfigurations(file_id, configs);
    }
    return true;
}

void RegionHandler::RemoveRegionRequirementsCache(int region_id) {
    // Clear requirements and cache for all regions or a specific region
    if (region_id == ALL_REGIONS) {
        _region_histograms.clear();
        _region_statistics.clear();

        std::unique_lock<std::mutex> spatial_lock(_spatial_mutex);
        _region_spatial_profiles.clear();
        spatial_lock.unlock();

        std::unique_lock<std::mutex> spectral_lock(_spectral_mutex);
        _spectral_req.clear();
        _spectral_cache.clear();
        spectral_lock.unlock();

        std::unique_lock pv_cut_lock(_pv_cut_mutex);
        _pv_preview_cuts.clear();
        std::unique_lock pv_cube_lock(_pv_cube_mutex);
        _pv_preview_cubes.clear();
    } else {
        // Remove region analysis for region_id
        if (_region_histograms.find(region_id) != _region_histograms.end()) {
            _region_histograms.erase(region_id);
        }

        if (_region_statistics.find(region_id) != _region_statistics.end()) {
            _region_statistics.erase(region_id);
        }

        std::unique_lock<std::mutex> spatial_lock(_spatial_mutex);
        if (_region_spatial_profiles.find(region_id) != _region_spatial_profiles.end()) {
            _region_spatial_profiles.erase(region_id);
        }
        spatial_lock.unlock();

        // Remove spectral requirements and cache for region_id
        std::unique_lock<std::mutex> spectral_lock(_spectral_mutex);
        for (auto it = _spectral_req.begin(); it != _spectral_req.end();) {
            if ((*it).first.region_id == region_id) {
                it = _spectral_req.erase(it);
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
        spectral_lock.unlock();

        // Remove PV preview cuts for region_id
        if (region_id > 0) {
            // Needed only for pv cut region in source image.
            std::unique_lock<std::shared_mutex> pv_cut_lock(_pv_cut_mutex);
            for (auto it = _pv_preview_cuts.begin(); it != _pv_preview_cuts.end();) {
                if ((*it).second->HasPreviewFileRegionIds(ALL_FILES, region_id)) {
                    it = _pv_preview_cuts.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }
}

void RegionHandler::RemoveFileRequirementsCache(int file_id) {
    // Clear requirements and cache for a specific file or for all files when closed
    if (file_id == ALL_FILES) {
        std::unique_lock<std::mutex> spatial_lock(_spatial_mutex);
        _region_spatial_profiles.clear();
        spatial_lock.unlock();

        _region_histograms.clear();
        _region_statistics.clear();

        std::unique_lock<std::mutex> spectral_lock(_spectral_mutex);
        _spectral_req.clear();
        _spectral_cache.clear();
        spectral_lock.unlock();

        std::unique_lock<std::shared_mutex> pv_cut_lock(_pv_cut_mutex);
        _pv_preview_cuts.clear();
        std::unique_lock<std::shared_mutex> pv_cube_lock(_pv_cube_mutex);
        _pv_preview_cubes.clear();
    } else {
        // Remove region analysis for file_id
        for (const auto& [_, region_histogram] : _region_histograms) {
            region_histogram->ClearFileConfigsCache(file_id);
        }

        for (const auto& [_, region_statistics] : _region_statistics) {
            region_statistics->ClearFileConfigsCache(file_id);
        }

        std::unique_lock<std::mutex> spatial_lock(_spatial_mutex);
        for (const auto& [_, spatial_profile] : _region_spatial_profiles) {
            spatial_profile->ClearFileConfigs(file_id);
        }
        spatial_lock.unlock();

        // Remove spectral requirements and cache for file_id
        std::unique_lock<std::mutex> spectral_lock(_spectral_mutex);
        for (auto it = _spectral_req.begin(); it != _spectral_req.end();) {
            if ((*it).first.file_id == file_id) {
                it = _spectral_req.erase(it);
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
        spectral_lock.unlock();

        // Remove PV preview cuts and cubes for file_id
        std::unique_lock<std::shared_mutex> pv_cut_lock(_pv_cut_mutex);
        for (auto it = _pv_preview_cuts.begin(); it != _pv_preview_cuts.end();) {
            if ((*it).second->HasPreviewFileRegionIds(file_id, ALL_REGIONS)) {
                it = _pv_preview_cuts.erase(it);
            } else {
                ++it;
            }
        }
        pv_cut_lock.unlock();

        std::unique_lock<std::shared_mutex> pv_cube_lock(_pv_cube_mutex);
        for (auto it = _pv_preview_cubes.begin(); it != _pv_preview_cubes.end();) {
            if ((*it).second->HasFileId(file_id)) {
                it = _pv_preview_cubes.erase(it);
            } else {
                ++it;
            }
        }
        pv_cube_lock.unlock();
    }
}

void RegionHandler::ClearRegionCache(int region_id) {
    // Remove cached data when region changes
    if (_region_histograms.find(region_id) != _region_histograms.end()) {
        _region_histograms[region_id]->ClearCache();
    }
    for (auto& spcache : _spectral_cache) {
        if (spcache.first.region_id == region_id) {
            spcache.second.ClearProfiles();
        }
    }

    if (_region_statistics.find(region_id) != _region_statistics.end()) {
        _region_statistics[region_id]->ClearCache();
    }
}

// ********************************************************************
// Region data stream helpers

bool RegionHandler::RegionFileIdsValid(int region_id, int file_id, bool check_annotation) {
    // Check error conditions and preconditions
    if (((region_id == ALL_REGIONS) && (file_id == ALL_FILES)) || (region_id == CURSOR_REGION_ID)) { // not allowed
        return false;
    }
    if (!RegionSet(region_id, check_annotation)) { // ID not found, Region is closing, or is annotation
        return false;
    }
    if (!FrameSet(file_id)) { // no Frame(s) for this id or Frame is closing
        return false;
    }
    return true;
}

// ********************************************************************
// Generated images

bool RegionHandler::CalculateMoments(int file_id, int region_id, const std::shared_ptr<Frame>& frame,
    GeneratorProgressCallback progress_callback, const CARTA::MomentRequest& moment_request, CARTA::MomentResponse& moment_response,
    std::vector<GeneratedImage>& collapse_results) {
    if (!RegionFileIdsValid(region_id, file_id, true)) {
        return false;
    }

    int stokes_index(frame->CurrentStokes());
    int z_min(moment_request.spectral_range().min());
    int z_max(moment_request.spectral_range().max());
    AxisRange z_range(z_min, z_max);
    StokesSource stokes_source(stokes_index, z_range);

    auto region = GetRegion(region_id);
    auto lc_region = region->GetLCRegion(file_id, frame->CoordinateSystem(stokes_source), frame->ImageShape(stokes_source), stokes_source);

    if (!lc_region) {
        return false;
    }

    casacore::ImageRegion image_region;
    if (!GetImageRegion(region, frame, z_range, stokes_index, lc_region, image_region)) {
        return false;
    }

    // Do calculations
    StokesRegion stokes_region(stokes_source, image_region);
    frame->CalculateMoments(
        file_id, progress_callback, stokes_region, moment_request, moment_response, collapse_results, region->GetRegionState());
    return !collapse_results.empty();
}

bool RegionHandler::CalculatePvImage(const CARTA::PvRequest& pv_request, std::shared_ptr<Frame> frame,
    GeneratorProgressCallback progress_callback, CARTA::PvResponse& pv_response, GeneratedImage& pv_image) {
    // Unpack request message and send it along
    int file_id(pv_request.file_id());
    int region_id(pv_request.region_id());
    int line_width(pv_request.width());
    bool reverse(pv_request.reverse());
    bool keep(pv_request.keep());
    AxisRange spectral_range;
    if (pv_request.has_spectral_range()) {
        spectral_range = AxisRange(pv_request.spectral_range().min(), pv_request.spectral_range().max());
    } else {
        spectral_range = AxisRange(0, frame->Depth() - 1);
    }
    bool is_preview(pv_request.has_preview_settings());

    // Initialize response if checks fail
    pv_response.set_success(false);
    pv_response.set_cancel(false);
    if (is_preview) {
        auto preview_id = pv_request.preview_settings().preview_id();
        auto* preview_data_message = pv_response.mutable_preview_data();
        preview_data_message->set_preview_id(preview_id);
        preview_data_message->set_width(0);
        preview_data_message->set_height(0);
    }

    // Checks for valid request:
    // 1. Region is set
    if (!RegionSet(region_id, true)) {
        pv_response.set_message("PV image requested for invalid region.");
        return false;
    }

    // 2. Region is line
    if (!IsLineRegion(region_id)) {
        pv_response.set_message("Region type not supported for PV cut.");
        return false;
    }

    // 3. Image has spectral axis
    if (!frame->CoordinateSystem()->hasSpectralAxis()) {
        pv_response.set_message("No spectral coordinate for generating PV image.");
        return false;
    }

    // 4. Valid width
    if (line_width < 1 || line_width > 20) {
        pv_response.set_message("Invalid averaging width.");
        return false;
    }

    // Set frame
    if (!FrameSet(file_id)) {
        _frames[file_id] = frame;
    }

    if (is_preview) {
        return CalculatePvPreviewImage(file_id, region_id, line_width, spectral_range, reverse, frame, pv_request.preview_settings(),
            progress_callback, pv_response, pv_image);
    } else {
        return CalculatePvImage(
            file_id, region_id, line_width, spectral_range, reverse, keep, frame, progress_callback, pv_response, pv_image);
    }
}

bool RegionHandler::CalculatePvPreviewImage(int file_id, int region_id, int line_width, AxisRange& spectral_range, bool reverse,
    std::shared_ptr<Frame>& frame, const CARTA::PvPreviewSettings& preview_settings, GeneratorProgressCallback progress_callback,
    CARTA::PvResponse& pv_response, GeneratedImage& pv_image) {
    // Find or set cached preview cube, then continue.
    // Unpack preview settings
    int preview_id(preview_settings.preview_id());
    int preview_region_id(preview_settings.region_id());
    int rebin_xy = std::max(preview_settings.rebin_xy(), 1);
    int rebin_z = std::max(preview_settings.rebin_z(), 1);
    auto compression = preview_settings.compression_type();
    float image_quality = preview_settings.image_compression_quality();
    float animation_quality = preview_settings.animation_compression_quality();

    // If not image region, check preview region and get its region state.
    bool is_image_region(preview_region_id == IMAGE_REGION_ID);
    std::shared_ptr<Region> preview_region;
    RegionState preview_region_state;
    if (!is_image_region) {
        if (!RegionSet(preview_region_id)) {
            pv_response.set_message("PV preview cube requested for invalid preview region id.");
            return false;
        }
        if (!IsClosedRegion(preview_region_id)) {
            pv_response.set_message("PV preview cube requested for invalid preview region type.");
            return false;
        }
        preview_region = GetRegion(preview_region_id);
        preview_region_state = preview_region->GetRegionState();
    }

    // Save cut and cube settings for updates, including current pv cut region state
    RegionState region_state = GetRegion(region_id)->GetRegionState();
    auto stokes_index = frame->CurrentStokes();
    PreviewCutParameters cut_parameters(
        file_id, region_id, line_width, reverse, compression, image_quality, animation_quality, region_state.reference_file_id);
    PreviewCubeParameters cube_parameters(
        file_id, preview_region_id, spectral_range, rebin_xy, rebin_z, stokes_index, preview_region_state);

    // Update cut and/or cube settings for existing preview ID.
    // Set unique locks so in-progress preview images are completed before update.
    std::unique_lock pv_cut_lock(_pv_cut_mutex);
    if (_pv_preview_cuts.find(preview_id) != _pv_preview_cuts.end() && _pv_preview_cuts.at(preview_id)->HasSameParameters(cut_parameters)) {
        // Same preview cut settings, clear queue and set this RegionState
        _pv_preview_cuts.at(preview_id)->AddRegion(region_state);
    } else {
        // Preview cut settings changed, set new PvPreviewCut
        _pv_preview_cuts[preview_id] = std::shared_ptr<PvPreviewCut>(new PvPreviewCut(cut_parameters, region_state));
    }
    auto preview_cut = _pv_preview_cuts.at(preview_id);
    pv_cut_lock.unlock();

    auto preview_frame_id = GetPvPreviewFrameId(preview_id);
    bool preview_frame_set = _frames.find(preview_frame_id) != _frames.end();

    std::unique_lock pv_cube_lock(_pv_cube_mutex);
    if (_pv_preview_cubes.find(preview_id) == _pv_preview_cubes.end() ||
        !_pv_preview_cubes.at(preview_id)->HasSameParameters(cube_parameters)) {
        // Preview cube changed, see if set for another preview ID
        bool cube_found(false);
        for (auto& preview_cube : _pv_preview_cubes) {
            if (preview_cube.second->HasSameParameters(cube_parameters)) {
                _pv_preview_cubes[preview_id] = preview_cube.second;
                cube_found = true;
                break;
            }
        }
        if (!cube_found) {
            _pv_preview_cubes[preview_id] = std::shared_ptr<PvPreviewCube>(new PvPreviewCube(cube_parameters));
        }

        // If preview cube changed, then frame for its preview image cube is invalid
        preview_frame_set = false;
    }

    auto preview_cube = _pv_preview_cubes.at(preview_id);
    bool preview_cube_loaded = preview_cube->CubeLoaded();
    pv_cube_lock.unlock();

    // Set frame for preview image if needed
    Timer t;
    if (!preview_frame_set || !preview_cube_loaded) {
        // Create or get cached preview image from PvPreviewCube. Progress callback for loading cube data if needed.
        bool cancel(false);
        std::string message;
        auto preview_image = preview_cube->GetPreviewImage(progress_callback, cancel, message);

        if (cancel) {
            pv_response.set_cancel(cancel);
            pv_response.set_message(message);
            return false;
        }

        if (!preview_image) {
            // Apply preview region or slicer to get SubImage, and set preview region origin.
            casacore::SubImage<float> sub_image;
            std::unique_lock<std::mutex> profile_lock(_line_profile_mutex);

            if (is_image_region) {
                // Apply slicer to source image to get SubImage
                auto slicer = frame->GetImageSlicer(spectral_range, stokes_index);
                if (!frame->GetSlicerSubImage(slicer, sub_image)) {
                    pv_response.set_message("Failed to set spectral range for preview cube.");
                    return false;
                }
                casacore::IPosition origin(2, 0, 0);
                preview_cube->SetPreviewRegionOrigin(origin);
            } else {
                // Apply preview LCRegion to source image to get SubImage
                StokesSource stokes_source(stokes_index, spectral_range);
                auto lc_region = preview_region->GetLCRegion(
                    preview_frame_id, frame->CoordinateSystem(stokes_source), frame->ImageShape(stokes_source), stokes_source);

                if (!lc_region) {
                    pv_response.set_message("Failed to set preview region for preview cube.");
                    return false;
                }

                // Origin (blc) for setting pv cut in cube
                auto origin = lc_region->boundingBox().start();
                preview_cube->SetPreviewRegionOrigin(origin);

                // Apply LCRegion and spectral range/stokes to source image to get ImageRegion
                casacore::ImageRegion image_region;
                if (!GetImageRegion(preview_region, frame, spectral_range, stokes_index, lc_region, image_region)) {
                    pv_response.set_message("Failed to set preview region or spectral range for preview cube.");
                    return false;
                }

                // Apply StokesRegion to source image to get SubImage
                StokesRegion stokes_region(stokes_source, image_region);
                if (!frame->GetRegionSubImage(stokes_region, sub_image)) {
                    pv_response.set_message("Failed to set preview region in image for preview cube.");
                    return false;
                }
            }

            // Get preview image from SubImage and downsampling parameters
            preview_image = preview_cube->GetPreviewImage(sub_image, progress_callback, cancel, message);
            if (!preview_image || cancel) {
                pv_response.set_cancel(cancel);
                pv_response.set_message(message);
                return false;
            }
            profile_lock.unlock();
        }

        // Preview image is now set, make frame to access it.
        auto preview_loader = std::shared_ptr<FileLoader>(FileLoader::GetLoader(preview_image, ""));
        auto preview_session_id(-1);
        auto preview_frame = std::make_shared<Frame>(preview_session_id, preview_loader, "");

        if (!preview_frame->IsValid()) {
            pv_response.set_message("Failed to load image from preview settings.");
        }

        _frames[preview_frame_id] = preview_frame;
    }
    spdlog::performance("PV preview cube and frame in {:.3f} ms", t.Elapsed().ms());

    bool quick_update(false);
    return CalculatePvPreviewImage(
        preview_frame_id, preview_id, quick_update, preview_cut, preview_cube, progress_callback, pv_response, pv_image);
}

bool RegionHandler::CalculatePvPreviewImage(int frame_id, int preview_id, bool quick_update, std::shared_ptr<PvPreviewCut> preview_cut,
    std::shared_ptr<PvPreviewCube> preview_cube, GeneratorProgressCallback progress_callback, CARTA::PvResponse& pv_response,
    GeneratedImage& pv_image) {
    // Calculate PV preview data using pv cut RegionState (in source image) and PvPreviewCube.
    // This method is the entry point for pv preview updates, where only the pv cut changed.
    // Get initial parameters to ensure cube did not change
    auto cube_parameters = preview_cube->parameters();

    // Prepare response; if error, add message.
    pv_response.set_success(false);
    pv_response.set_cancel(false);
    auto* preview_data_message = pv_response.mutable_preview_data();
    preview_data_message->set_preview_id(preview_id);
    preview_data_message->set_width(0);
    preview_data_message->set_height(0);

    RegionState source_region_state;
    if (!preview_cut->GetNextRegion(source_region_state)) {
        spdlog::info("No PV cut regions queued for pv preview");
        return false;
    }

    // Set new pv cut region in preview image.
    int preview_cut_id(TEMP_REGION_ID);
    auto preview_cut_state = preview_cube->GetPvCutRegion(source_region_state, frame_id);
    auto preview_frame = _frames.at(frame_id);
    auto preview_frame_csys = preview_frame->CoordinateSystem();
    if (!SetRegion(preview_cut_id, preview_cut_state, preview_frame_csys)) {
        pv_response.set_message("Failed to set line region in preview cube.");
        return false;
    }

    LineBoxRegions line_box_regions;
    auto line_width = preview_cut->GetWidth();
    casacore::Quantity increment;
    std::vector<RegionState> box_regions;
    std::string error;

    // Approximate preview cut region with line width as series of box regions
    if (!line_box_regions.GetLineBoxRegions(preview_cut_state, preview_frame_csys, line_width, increment, box_regions, error)) {
        spdlog::debug("GetLineBoxRegions failed!");
        spdlog::error(error);
        pv_response.set_message(error);
        RemoveRegion(preview_cut_id);
        return false;
    }

    // Initialize preview data then set with box region profiles
    std::vector<float> preview_data;
    size_t num_regions(box_regions.size());
    auto depth = preview_frame->Depth();

    auto reverse = preview_cut->GetReverse();
    casacore::IPosition data_shape;
    if (reverse) {
        data_shape = casacore::IPosition(2, depth, num_regions);
    } else {
        data_shape = casacore::IPosition(2, num_regions, depth);
    }

    // Set preview data as a matrix row/column (shared memory), initialized to NaN if any region profile fails
    preview_data.resize(data_shape.product());
    casacore::Matrix<float> preview_data_matrix(data_shape, preview_data.data(), casacore::StorageInitPolicy::SHARE);
    preview_data_matrix = FLOAT_NAN;

    // Do not collide with line spatial profile (coord sys copy crash)
    std::unique_lock<std::mutex> profile_lock(_line_profile_mutex);
    for (size_t iregion = 0; iregion < num_regions; ++iregion) {
        // Set box region with next temp region id
        int box_region_id(TEMP_REGION_ID);
        SetRegion(box_region_id, box_regions[iregion], preview_frame_csys);

        if (!RegionSet(box_region_id)) {
            continue;
        }
        auto box_region = GetRegion(box_region_id);

        // Get box region LCRegion and mask
        bool cancel(false);
        auto box_lc_region = box_region->GetLCRegion(frame_id, preview_frame->CoordinateSystem(), preview_frame->ImageShape());

        if (!box_lc_region) {
            RemoveRegion(box_region_id);
            continue;
        }

        auto bounding_box = box_lc_region->boundingBox();
        auto box_mask = box_region->GetImageRegionMask(frame_id);
        RemoveRegion(box_region_id);

        // Use PvPreviewCube to calculate profile with lc_region and mask
        std::vector<float> profile;
        double max_num_pixels(0.0);
        std::string message;

        // Make sure preview cube exists and has not changed
        std::unique_lock pv_cube_lock(_pv_cube_mutex);
        if (preview_cube && preview_cube->HasSameParameters(cube_parameters)) {
            // Progress for loading data here if needed due to prior cancel
            if (preview_cube->GetRegionProfile(bounding_box, box_mask, progress_callback, profile, max_num_pixels, message)) {
                // spdlog::debug("PV preview profile {} of {} max num pixels={}", iregion, num_regions, max_num_pixels);
                casacore::Vector<float> const profile_v(profile);
                if (reverse) {
                    preview_data_matrix.column(iregion) = profile_v;
                } else {
                    preview_data_matrix.row(iregion) = profile_v;
                }
            }
        }
        pv_cube_lock.unlock();

        if (cancel) {
            RemoveRegion(preview_cut_id);
            pv_response.set_message(message);
            pv_response.set_cancel(true);
            return false;
        }
    }

    profile_lock.unlock();
    RemoveRegion(preview_cut_id);

    // Use PvGenerator to set PV image for headers only
    PvGenerator::PositionAxisType pos_axis_type = (preview_cut_state.type == CARTA::LINE ? PvGenerator::OFFSET : PvGenerator::DISTANCE);
    casacore::Matrix<float> no_preview_data; // do not copy actual preview data into image
    int start_channel(0);                    // spectral range applied in preview image
    int stokes_index(preview_cube->GetStokes());
    PvGenerator pv_generator;
    pv_generator.SetFileName(preview_id, preview_cube->GetSourceFileName(), true);

    if (pv_generator.GetPvImage(
            preview_frame, no_preview_data, data_shape, pos_axis_type, increment, start_channel, stokes_index, reverse, pv_image, error)) {
        int width = data_shape(0);
        int height = data_shape(1);

        // Compress preview data if requested, else just fill message
        if (preview_cut->FillCompressedPreviewData(preview_data_message, preview_data, width, height, quick_update)) {
            // Calculate histogram bounds
            int num_bins = int(std::max(sqrt(data_shape(0) * data_shape(1)), 2.0));
            BasicStats<float> basic_stats;
            CalcBasicStats(basic_stats, preview_data.data(), preview_data.size());
            HistogramBounds bounds(basic_stats.min_val, basic_stats.max_val);
            Histogram hist = CalcHistogram(num_bins, bounds, preview_data.data(), preview_data.size());
            CARTA::FloatBounds hist_bounds;
            hist_bounds.set_min(hist.GetMinVal());
            hist_bounds.set_max(hist.GetMaxVal());

            // Complete PvResponse
            pv_response.set_success(true);
            preview_data_message->set_width(width);
            preview_data_message->set_height(height);
            *preview_data_message->mutable_histogram_bounds() = hist_bounds;

            // Add Histogram if not doing quick update
            if (!quick_update) {
                auto preview_histogram = preview_data_message->mutable_histogram();
                FillHistogram(preview_histogram, basic_stats, hist);
            }
        } else {
            pv_response.set_success(false);
            pv_response.set_message("Preview data compression failed: unsupported type");
            return false;
        }
    } else {
        pv_response.set_success(false);
        pv_response.set_message(error);
        return false;
    }

    return true;
}

bool RegionHandler::CalculatePvImage(int file_id, int region_id, int width, AxisRange& spectral_range, bool reverse, bool keep,
    std::shared_ptr<Frame>& frame, GeneratorProgressCallback progress_callback, CARTA::PvResponse& pv_response, GeneratedImage& pv_image) {
    // Generate PV image by approximating line/polyline as box regions and getting spectral profile for each.
    // Sends updates via progress callback.
    // Return parameters: PvResponse, GeneratedImage, and preview data if is preview.
    // Returns whether PV image was generated.
    pv_response.set_success(false);
    pv_response.set_cancel(false);

    auto region = GetRegion(region_id);
    if (!region) {
        pv_response.set_message("PV cut region not set");
        return false;
    }
    auto cut_region_type = region->GetRegionState().type;

    // Reset stop flag
    _stop_pv[file_id] = false;

    // Common parameters for PV and preview
    bool pv_success(false), cancelled(false);
    int stokes_index = frame->CurrentStokes();
    casacore::Quantity offset_increment;
    casacore::Matrix<float> pv_data;
    std::string message;
    if (spectral_range.to == ALL_Z) {
        spectral_range.to = frame->Depth() - 1;
    }

    if (GetLineProfiles(file_id, region_id, width, spectral_range, stokes_index, "", progress_callback, pv_data, offset_increment,
            cancelled, message, reverse)) {
        auto pv_shape = pv_data.shape();
        PvGenerator::PositionAxisType pos_axis_type = (cut_region_type == CARTA::LINE ? PvGenerator::OFFSET : PvGenerator::DISTANCE);
        int start_chan(spectral_range.from); // Used for reference value in returned PV image

        // Set PV index suffix (_pv1, _pv2, etc) to keep previously opened PV image
        int name_index(0);
        if (keep && (_pv_name_index.find(file_id) != _pv_name_index.end())) {
            name_index = ++_pv_name_index[file_id];
        }
        _pv_name_index[file_id] = name_index;

        std::shared_lock frame_lock(frame->GetActiveTaskMutex());
        auto source_filename = frame->GetFileName();

        // Create GeneratedImage in PvGenerator
        PvGenerator pv_generator;
        pv_generator.SetFileName(name_index, source_filename);
        pv_success = pv_generator.GetPvImage(
            frame, pv_data, pv_shape, pos_axis_type, offset_increment, start_chan, stokes_index, reverse, pv_image, message);
        cancelled &= _stop_pv[file_id];

        // Cleanup
        _stop_pv.erase(file_id);

        // Close source image if on disk and not used elsewhere
        if (!source_filename.empty()) {
            frame->CloseCachedImage(source_filename);
        }
        frame_lock.unlock();
    }

    if (cancelled) {
        pv_success = false;
        message = "PV image generator cancelled.";
        cancelled = true;
        spdlog::debug(message);
    }

    // Complete message
    pv_response.set_success(pv_success);
    pv_response.set_message(message);
    pv_response.set_cancel(cancelled);
    return pv_success;
}

bool RegionHandler::UpdatePvPreviewRegion(int region_id, RegionState& region_state) {
    // Set region state in PvPreviewCut, if region is pv cut.  Returns this status.
    if (region_state.type != CARTA::RegionType::LINE) {
        return false;
    }
    std::shared_lock pv_cut_lock(_pv_cut_mutex);
    bool is_preview_cut(false);
    for (auto& preview_cut : _pv_preview_cuts) {
        if (preview_cut.second->HasPreviewCutRegion(region_id, region_state.reference_file_id)) {
            preview_cut.second->AddRegion(region_state);
            is_preview_cut = true;
        }
    }
    return is_preview_cut;
}

bool RegionHandler::UpdatePvPreviewImage(
    int file_id, int region_id, bool no_histogram, std::function<void(CARTA::PvResponse& pv_response, GeneratedImage& pv_image)> cb) {
    // Update all previews using the pv cut described by file and region IDs
    bool preview_updated(false);

    // Lock settings to prevent removal/replacement until queue is complete
    std::shared_lock pv_cut_lock(_pv_cut_mutex);

    for (auto& pv_preview_cut : _pv_preview_cuts) {
        // Find and update pv preview settings with input file and region
        auto preview_cut = pv_preview_cut.second;
        if (preview_cut->HasPreviewFileRegionIds(file_id, region_id)) {
            auto preview_id = pv_preview_cut.first;

            if (_pv_preview_cubes.find(preview_id) == _pv_preview_cubes.end()) {
                spdlog::debug("No preview cube found");
                return preview_updated;
            }
            auto preview_cube = _pv_preview_cubes.at(preview_id);

            auto frame_id = GetPvPreviewFrameId(preview_id);
            if (_frames.find(frame_id) == _frames.end()) {
                spdlog::debug("No preview cube frame found");
                return preview_updated;
            }

            if (preview_cut->HasQueuedRegion() && preview_cube->CubeLoaded()) {
                // Generate preview for one region state in queue
                spdlog::debug("Updating pv preview {} for region {}", preview_id, region_id);
                GeneratorProgressCallback progress_callback = [](float progress) {}; // no progress for preview update
                CARTA::PvResponse pv_response;
                GeneratedImage pv_image;
                preview_updated = CalculatePvPreviewImage(
                    frame_id, preview_id, no_histogram, preview_cut, preview_cube, progress_callback, pv_response, pv_image);
                cb(pv_response, pv_image);
            } else {
                spdlog::debug("PV preview {} failed: cube data not loaded or no preview regions queued", preview_id);
            }
        }
    }

    return preview_updated;
}

int RegionHandler::GetPvPreviewFrameId(int preview_id) {
    return preview_id + TEMP_FILE_ID;
}

void RegionHandler::StopPvCalc(int file_id) {
    // Cancel any PV calculations in progress
    _stop_pv[file_id] = true;
}

void RegionHandler::StopPvPreview(int preview_id) {
    // Cancel loading preview cube cache
    if (_pv_preview_cubes.find(preview_id) != _pv_preview_cubes.end()) {
        _pv_preview_cubes.at(preview_id)->StopCube();
    }
}

void RegionHandler::StopPvPreviewUpdates(int preview_id) {
    // Clear region queue to stop pv preview updates
    if (_pv_preview_cuts.find(preview_id) != _pv_preview_cuts.end()) {
        // Safe because has internal mutex for queue
        _pv_preview_cuts.at(preview_id)->ClearRegionQueue();
    }
}

void RegionHandler::ClosePvPreview(int preview_id) {
    // Cancel PV calculations and remove preview settings, frame, and stop flag
    StopPvPreviewUpdates(preview_id);
    StopPvPreview(preview_id);

    std::unique_lock pv_cut_lock(_pv_cut_mutex);
    if (_pv_preview_cuts.find(preview_id) != _pv_preview_cuts.end()) {
        _pv_preview_cuts.erase(preview_id);
    }
    pv_cut_lock.unlock();

    std::unique_lock pv_cube_lock(_pv_cube_mutex);
    if (_pv_preview_cubes.find(preview_id) != _pv_preview_cubes.end()) {
        _pv_preview_cubes.erase(preview_id);
    }
    pv_cube_lock.unlock();

    auto frame_id = GetPvPreviewFrameId(preview_id);
    _frames.erase(frame_id);
}

bool RegionHandler::FitImage(const CARTA::FittingRequest& fitting_request, CARTA::FittingResponse& fitting_response,
    std::shared_ptr<Frame> frame, GeneratedImage& model_image, GeneratedImage& residual_image,
    GeneratorProgressCallback progress_callback) {
    int file_id(fitting_request.file_id());
    int region_id(fitting_request.region_id());

    if (region_id == 0) {
        region_id = TEMP_FOV_REGION_ID;

        auto fov_info(fitting_request.fov_info());
        std::vector<CARTA::Point> points = {fov_info.control_points().begin(), fov_info.control_points().end()};
        RegionState region_state(fitting_request.file_id(), fov_info.region_type(), points, fov_info.rotation());
        auto csys = frame->CoordinateSystem();

        if (!SetRegion(region_id, region_state, csys)) {
            spdlog::error("Failed to set up field of view region!");
            fitting_response.set_message("failed to set up field of view region");
            fitting_response.set_success(false);
            return false;
        }
    } else if (region_id < 0 || !RegionSet(region_id)) {
        fitting_response.set_message("region id not found");
        fitting_response.set_success(false);
        return false;
    }

    // Save frame pointer
    _frames[file_id] = frame;

    AxisRange z_range(frame->CurrentZ());
    int stokes_index = frame->CurrentStokes();
    StokesSource stokes_source(stokes_index, z_range);

    auto region = GetRegion(region_id);
    auto lc_region = region->GetLCRegion(file_id, frame->CoordinateSystem(stokes_source), frame->ImageShape(stokes_source), stokes_source);
    bool success(false);

    if (!lc_region) {
        fitting_response.set_message("region is outside image or is not closed");
        fitting_response.set_success(false);
    } else {
        casacore::ImageRegion image_region;
        if (GetImageRegion(region, frame, z_range, stokes_index, lc_region, image_region)) {
            StokesRegion stokes_region(stokes_source, image_region);
            success = frame->FitImage(fitting_request, fitting_response, model_image, residual_image, progress_callback, &stokes_region);
        } else {
            fitting_response.set_message("Setting region z and stokes failed");
            fitting_response.set_success(false);
        }
    }

    if (region_id == TEMP_FOV_REGION_ID) {
        RemoveRegion(region_id);
    }

    return success;
}

// ********************************************************************
// Fill data stream messages:
// These always use a callback since there may be multiple region/file requirements
// region_id > 0 file_id >= 0   update data for specified region/file
// region_id > 0 file_id < 0    update data for all files in region's requirements (region changed)
// region_id < 0 file_id >= 0   update data for all regions with file_id (z/stokes changed)
// region_id < 0 file_id < 0    not allowed (all regions for all files?)
// region_id = 0                not allowed (cursor region handled by Frame)

bool RegionHandler::FillRegionHistogramData(std::function<void(CARTA::RegionHistogramData histogram_data)> cb, int region_id, int file_id) {
    if (!RegionFileIdsValid(region_id, file_id, true)) {
        return false;
    }

    if ((region_id > 0) && (_region_histograms.find(region_id) == _region_histograms.end())) {
        return false;
    }

    bool success(false);
    for (const auto& [hist_region_id, region_histogram] : _region_histograms) {
        // Find histogram configurations with region_id and file_id
        if ((region_id > 0) && (hist_region_id != region_id)) {
            continue;
        }

        auto config_file_ids = region_histogram->GetConfigFileIds(file_id);
        if (config_file_ids.empty()) {
            continue;
        }

        for (int hist_file_id : config_file_ids) {
            // Get histogram for specific region id and file id
            if (!RegionFileIdsValid(hist_region_id, hist_file_id)) {
                continue;
            }

            auto frame = _frames.at(hist_file_id);

            std::vector<HistogramConfig> histogram_configs;
            if (!region_histogram->GetConfigurations(hist_file_id, histogram_configs)) {
                continue;
            }

            for (auto& histogram_config : histogram_configs) {
                // Create data message for each configuration
                int stokes_index(0);
                if (!frame->GetStokesTypeIndex(histogram_config.coordinate, stokes_index)) {
                    continue;
                }

                // Get StokesRegion
                int z = (histogram_config.channel == CURRENT_Z ? frame->CurrentZ() : histogram_config.channel);
                AxisRange z_range(z);
                StokesSource stokes_source(stokes_index, z_range);

                auto region = GetRegion(hist_region_id);
                auto lc_region = region->GetLCRegion(
                    hist_file_id, frame->CoordinateSystem(stokes_source), frame->ImageShape(stokes_source), stokes_source);

                casacore::ImageRegion image_region;
                GetImageRegion(region, frame, z_range, stokes_index, lc_region, image_region);
                StokesRegion stokes_region(stokes_source, image_region);

                // Fill histogram message
                CARTA::RegionHistogramData histogram_data_message;
                if (region_histogram->GetRegionHistogramData(
                        hist_file_id, frame, histogram_config, stokes_region, histogram_data_message)) {
                    cb(histogram_data_message);
                    success = true;
                }
            }
        }
    }
    return success;
}

// ***** Fill spectral profile *****

bool RegionHandler::FillSpectralProfileData(
    std::function<void(CARTA::SpectralProfileData profile_data)> cb, int region_id, int file_id, bool stokes_changed) {
    // Fill spectral profiles for given region and file ids.  This could be:
    // 1. a specific region and a specific file
    // 2. a specific region and ALL_FILES
    // 3. a specific file and ALL_REGIONS
    if (!RegionFileIdsValid(region_id, file_id, true)) {
        return false;
    }

    std::unique_lock<std::mutex> ulock(_spectral_mutex);
    std::unordered_map<ConfigId, RegionSpectralConfig, ConfigIdHash> region_configs;
    region_configs.insert(_spectral_req.begin(), _spectral_req.end());
    ulock.unlock();

    bool profile_ok(false);
    // Fill spectral profile for region with file requirement
    for (auto& region_config : region_configs) {
        if (region_config.second.configs.empty()) {
            // no spectral requirements for this region/file combo
            continue;
        }

        int config_region_id(region_config.first.region_id);
        int config_file_id(region_config.first.file_id);

        if (((config_region_id == region_id) && ((config_file_id == file_id) || (file_id == ALL_FILES))) ||
            ((config_file_id == file_id) && (region_id == ALL_REGIONS))) {
            // Found matching requirement

            if (!RegionFileIdsValid(config_region_id, config_file_id)) { // check specific ids
                continue;
            }

            for (auto& spectral_config : region_config.second.configs) {
                // Determine which profiles to send
                std::string coordinate(spectral_config.coordinate);
                std::vector<CARTA::StatsType> required_stats;
                if (stokes_changed) {
                    if (coordinate != "z") { // Do not update when stokes changes for fixed stokes
                        continue;
                    }
                    // Update all profiles for current stokes
                    required_stats = spectral_config.all_stats;
                } else {
                    // Update only new profiles
                    required_stats = spectral_config.new_stats;
                }

                if (required_stats.empty()) {
                    // no requirements for this config or no new stats to send
                    profile_ok = true;
                    continue;
                }

                int stokes_index;
                if (!_frames.at(config_file_id)->GetStokesTypeIndex(coordinate, stokes_index)) {
                    continue;
                }

                // Return spectral profile for this requirement
                bool report_error(true);
                AxisRange z_range(0, _frames.at(config_file_id)->Depth() - 1); // all channels
                profile_ok = GetRegionSpectralData(config_region_id, config_file_id, z_range, coordinate, stokes_index, required_stats,
                    report_error, [&](std::map<CARTA::StatsType, std::vector<double>> results, float progress) {
                        auto profile_message = Message::SpectralProfileData(config_file_id, config_region_id, stokes_index, progress);
                        Message::AddSpectralProfile(profile_message, coordinate, required_stats, results);
                        cb(profile_message); // send (partial profile) data
                    });
            }
        }
    }

    return profile_ok;
}

bool RegionHandler::GetRegionSpectralData(int region_id, int file_id, const AxisRange& z_range, std::string& coordinate, int stokes_index,
    std::vector<CARTA::StatsType>& required_stats, bool report_error,
    const std::function<void(std::map<CARTA::StatsType, std::vector<double>>, float)>& partial_results_callback) {
    // Fill spectral profile message for given region, file, and requirement
    if (!RegionFileIdsValid(region_id, file_id, true)) {
        return false;
    }

    // Check cancel
    if (!HasSpectralRequirements(region_id, file_id, coordinate, required_stats)) {
        return false;
    }

    bool use_current_stokes(coordinate == "z");

    Timer t;

    auto frame = _frames.at(file_id);
    std::shared_lock frame_lock(frame->GetActiveTaskMutex());
    auto region = GetRegion(region_id);
    std::shared_lock region_lock(region->GetActiveTaskMutex());

    // Initialize results map for requested stats to NaN, progress to zero
    size_t profile_end = z_range.to;
    size_t profile_size = z_range.to - z_range.from + 1;
    std::vector<double> init_spectral(profile_size, DOUBLE_NAN);
    std::map<CARTA::StatsType, std::vector<double>> results;
    for (const auto& stat : required_stats) {
        results[stat] = init_spectral;
    }
    float progress(0.0);

    // Check cache
    CacheId cache_id(file_id, region_id, stokes_index);
    if (_spectral_cache.count(cache_id) && !_spectral_cache[cache_id].profiles.empty()) {
        // Copy profiles to results map
        for (auto& result : results) {
            auto stats_type = result.first;
            std::vector<double> profile;
            if (_spectral_cache[cache_id].GetProfile(stats_type, profile)) {
                results[stats_type] = profile;
            }
        }
        progress = 1.0;
        partial_results_callback(results, progress);
        return true;
    }

    // Get 2D region with original image coordinate to check if inside image and whether to use loader
    StokesSource stokes_source(stokes_index, z_range);
    auto lc_region = region->GetLCRegion(file_id, frame->CoordinateSystem(stokes_source), frame->ImageShape(stokes_source), stokes_source);

    if (!lc_region) {
        // region outside image, send NaNs
        progress = 1.0;
        partial_results_callback(results, progress);
        return true;
    }

    // Get initial region info to cancel profile if it changes
    RegionState initial_region_state = region->GetRegionState();

    // Use loader swizzled data for efficiency
    if (frame->UseLoaderSpectralData(lc_region->shape())) {
        // Use cursor spectral profile for point region
        if (initial_region_state.type == CARTA::RegionType::POINT) {
            casacore::IPosition origin = lc_region->boundingBox().start();
            auto point = Message::Point(origin(0), origin(1));

            auto get_stokes_profiles_data = [&](ProfilesMap& tmp_results, int tmp_stokes_index) {
                std::vector<float> tmp_profile;
                if (!frame->GetLoaderPointSpectralData(tmp_profile, tmp_stokes_index, point)) {
                    return false;
                }
                // Set results; there is only one required stat for point
                std::vector<double> tmp_data(tmp_profile.begin(), tmp_profile.end());
                tmp_results[required_stats[0]] = tmp_data;
                return true;
            };

            auto get_profiles_data = [&](ProfilesMap& tmp_results, std::string tmp_coordinate) {
                int tmp_stokes_index;
                return (
                    frame->GetStokesTypeIndex(tmp_coordinate, tmp_stokes_index) && get_stokes_profiles_data(tmp_results, tmp_stokes_index));
            };

            if (Stokes::IsComputed(stokes_index)) { // For computed stokes
                if (!GetComputedStokesProfiles(results, stokes_index, get_profiles_data)) {
                    return false;
                }
            } else { // For regular stokes I, Q, U, or V
                if (!get_stokes_profiles_data(results, stokes_index)) {
                    return false;
                }
            }

            partial_results_callback(results, 1.0);
            return true;
        }

        // Get 2D origin and 2D mask for Hdf5Loader
        casacore::IPosition origin = lc_region->boundingBox().start();
        casacore::IPosition xy_origin = origin.keepAxes(casacore::IPosition(2, 0, 1)); // keep first two axes only

        // Get mask; LCRegion for file id is cached
        casacore::ArrayLattice<casacore::Bool> mask = region->GetImageRegionMask(file_id);
        if (!mask.shape().empty()) {
            // start the timer
            auto t_start = std::chrono::high_resolution_clock::now();
            auto t_latest = t_start;

            // Get partial profiles until complete (do once if cached)
            while (progress < 1.0) {
                // Cancel if region or frame is closing
                if (!RegionFileIdsValid(region_id, file_id)) {
                    return false;
                }

                // Cancel if region, current stokes, or spectral requirements changed
                if (region->GetRegionState() != initial_region_state) {
                    return false;
                }
                if (use_current_stokes && (stokes_index != frame->CurrentStokes())) {
                    return false;
                }
                if (!HasSpectralRequirements(region_id, file_id, coordinate, required_stats)) {
                    return false;
                }

                // Get partial profile
                auto get_profiles_data = [&](ProfilesMap& tmp_results, std::string tmp_coordinate) {
                    int tmp_stokes_index;
                    return (frame->GetStokesTypeIndex(tmp_coordinate, tmp_stokes_index) &&
                            frame->GetLoaderSpectralData(region_id, z_range, tmp_stokes_index, mask, xy_origin, tmp_results, progress));
                };

                ProfilesMap partial_profiles;
                if (Stokes::IsComputed(stokes_index)) { // For computed stokes
                    if (!GetComputedStokesProfiles(partial_profiles, stokes_index, get_profiles_data)) {
                        return false;
                    }
                } else { // For regular stokes I, Q, U, or V
                    if (!frame->GetLoaderSpectralData(region_id, z_range, stokes_index, mask, xy_origin, partial_profiles, progress)) {
                        return false;
                    }
                }

                // get the time elapse for this step
                auto t_end = std::chrono::high_resolution_clock::now();
                auto dt = std::chrono::duration<double, std::milli>(t_end - t_latest).count();

                if ((dt > TARGET_PARTIAL_REGION_TIME) || (progress >= 1.0)) {
                    // Copy partial profile to results
                    for (const auto& profile : partial_profiles) {
                        auto stats_type = profile.first;
                        if (results.count(stats_type)) {
                            results[stats_type] = profile.second;
                        }
                    }

                    // restart timer
                    t_latest = t_end;

                    // send partial result
                    partial_results_callback(results, progress);
                }
            }

            spdlog::performance("Fill spectral profile in {:.3f} ms", t.Elapsed().ms());
            return true;
        }
    } // end loader swizzled data

    // Initialize cache results for *all* spectral stats
    std::map<CARTA::StatsType, std::vector<double>> cache_results;
    for (const auto& stat : _spectral_stats) {
        cache_results[stat] = init_spectral;
    }

    // Calculate and cache profiles
    size_t start_z(z_range.from), count(0), end_z(0), profile_start(0);
    int delta_z = INIT_DELTA_Z;        // the increment of z for each step
    int dt_target = TARGET_DELTA_TIME; // the target time elapse for each step, in the unit of milliseconds
    auto t_partial_profile_start = std::chrono::high_resolution_clock::now();

    if (Stokes::IsComputed(stokes_index)) { // Need to re-calculate the lattice coordinate region for computed stokes index
        lc_region = nullptr;
    }

    // Get per-z stats data for spectral profiles
    while (progress < 1.0) {
        // start the timer
        auto t_start = std::chrono::high_resolution_clock::now();

        end_z = (start_z + delta_z > profile_end ? profile_end : start_z + delta_z - 1);
        count = end_z - start_z + 1;

        // Get 3D region for z range and stokes_index
        AxisRange partial_z_range(start_z, end_z);

        auto get_stokes_profiles_data = [&](ProfilesMap& tmp_partial_profiles, int tmp_stokes_index) {
            casacore::ImageRegion image_region;
            bool per_z(true); // Get per-z stats data for region for all stats (for cache)
            if (GetImageRegion(region, frame, partial_z_range, tmp_stokes_index, lc_region, image_region)) {
                StokesSource stokes_source(tmp_stokes_index, partial_z_range);
                StokesRegion stokes_region(stokes_source, image_region);
                return frame->GetRegionStats(stokes_region, _spectral_stats, per_z, tmp_partial_profiles);
            }
            return false;
        };

        auto get_profiles_data = [&](ProfilesMap& tmp_partial_profiles, std::string tmp_coordinate) {
            int tmp_stokes_index;
            return (frame->GetStokesTypeIndex(tmp_coordinate, tmp_stokes_index) &&
                    get_stokes_profiles_data(tmp_partial_profiles, tmp_stokes_index));
        };

        ProfilesMap partial_profiles;
        if (Stokes::IsComputed(stokes_index)) { // For computed stokes
            if (!GetComputedStokesProfiles(partial_profiles, stokes_index, get_profiles_data)) {
                return false;
            }
        } else { // For regular stokes I, Q, U, or V
            if (!get_stokes_profiles_data(partial_profiles, stokes_index)) {
                return false;
            }
        }

        // Copy partial profile to results and cache_results (all stats)
        for (const auto& profile : partial_profiles) {
            auto stats_type = profile.first;
            const std::vector<double>& stats_data = profile.second;
            if (results.count(stats_type)) {
                memcpy(&results[stats_type][profile_start], &stats_data[0], stats_data.size() * sizeof(double));
            }
            memcpy(&cache_results[stats_type][profile_start], &stats_data[0], stats_data.size() * sizeof(double));
        }

        start_z += count;
        profile_start += count;
        progress = (float)profile_start / profile_size;

        // get the time elapse for this step
        auto t_end = std::chrono::high_resolution_clock::now();
        auto dt = std::chrono::duration<double, std::milli>(t_end - t_start).count();
        auto dt_partial_profile = std::chrono::duration<double, std::milli>(t_end - t_partial_profile_start).count();

        // adjust the increment of z according to the time elapse
        delta_z *= dt_target / dt;
        if (delta_z < 1) {
            delta_z = 1;
        }
        if (delta_z > profile_size) {
            delta_z = profile_size;
        }

        // Cancel if region or frame is closing
        if (!RegionFileIdsValid(region_id, file_id)) {
            return false;
        }

        // Cancel if region, current stokes, or spectral requirements changed
        if (region->GetRegionState() != initial_region_state) {
            return false;
        }
        if (use_current_stokes && (stokes_index != _frames.at(file_id)->CurrentStokes())) {
            return false;
        }
        if (!HasSpectralRequirements(region_id, file_id, coordinate, required_stats)) {
            return false;
        }

        // send partial result by the callback function
        if (dt_partial_profile > TARGET_PARTIAL_REGION_TIME || progress >= 1.0) {
            t_partial_profile_start = std::chrono::high_resolution_clock::now();
            partial_results_callback(results, progress);
            if (progress >= 1.0) {
                // cache results for all stats types
                // TODO: cache and load partial profiles
                _spectral_cache[cache_id] = SpectralCache(cache_results);
            }
        }
    }

    spdlog::performance("Fill spectral profile in {:.3f} ms", t.Elapsed().ms());
    return true;
}

bool RegionHandler::FillRegionStatsData(std::function<void(CARTA::RegionStatsData stats_data)> cb, int region_id, int file_id) {
    // Fill stats data for given region and file
    if (!RegionFileIdsValid(region_id, file_id, true)) {
        return false;
    }

    if ((region_id > 0) && (_region_statistics.find(region_id) == _region_statistics.end())) {
        return false;
    }

    bool success(false);

    for (const auto& [stats_region_id, region_statistics] : _region_statistics) {
        if ((region_id > 0) && (stats_region_id != region_id)) {
            continue;
        }

        auto config_file_ids = region_statistics->GetConfigFileIds(file_id);
        if (config_file_ids.empty()) {
            continue;
        }

        auto region = GetRegion(stats_region_id);

        for (int stats_file_id : config_file_ids) {
            // Get statistics for specific region id and file id
            if (!RegionFileIdsValid(stats_region_id, stats_file_id)) {
                continue;
            }

            auto frame = _frames.at(stats_file_id);

            std::vector<CARTA::SetStatsRequirements_StatsConfig> stats_configs;
            if (!region_statistics->GetConfigurations(stats_file_id, stats_configs)) {
                continue;
            }

            // Create data message for each configuration
            for (auto& stats_config : stats_configs) {
                // Get stokes and channel from config
                int stokes_index(0);
                if (!frame->GetStokesTypeIndex(stats_config.coordinate(), stokes_index)) {
                    continue; // invalid image/computed Stokes
                }

                // Get StokesRegion
                int z(_frames.at(stats_file_id)->CurrentZ());
                AxisRange z_range(z);
                StokesSource stokes_source(stokes_index, z_range);
                auto lc_region = region->GetLCRegion(
                    stats_file_id, frame->CoordinateSystem(stokes_source), frame->ImageShape(stokes_source), stokes_source);

                casacore::ImageRegion image_region;
                GetImageRegion(region, frame, z_range, stokes_index, lc_region, image_region);
                StokesRegion stokes_region(stokes_source, image_region);
                CARTA::RegionStatsData stats_data_message;

                if (region_statistics->GetRegionStatsData(stats_file_id, frame, stats_config, stokes_region, stats_data_message)) {
                    cb(stats_data_message);
                    success = true;
                }
            }
        }
    }

    return success;
}

bool RegionHandler::FillSpatialProfileData(std::function<void(CARTA::SpatialProfileData profile_data)> cb, int file_id, int region_id) {
    Timer t;
    if (!RegionFileIdsValid(region_id, file_id, true)) {
        return false;
    }

    std::vector<int> spatial_region_ids;
    std::unique_lock<std::mutex> ulock(_spatial_mutex);
    if (region_id > 0) {
        if (_region_spatial_profiles.find(region_id) == _region_spatial_profiles.end()) {
            return false;
        }
        spatial_region_ids.push_back(region_id);
    } else {
        for (const auto& [spatial_region_id, _] : _region_spatial_profiles) {
            // Get actual region ids when region_id == ALL_REGIONS
            spatial_region_ids.push_back(spatial_region_id);
        }
    }
    ulock.unlock();

    bool success(false);
    for (int spatial_region_id : spatial_region_ids) {
        // Get region spatial profile
        std::unique_lock<std::mutex> ulock(_spatial_mutex);
        if (_region_spatial_profiles.find(spatial_region_id) == _region_spatial_profiles.end()) {
            ulock.unlock();
            continue;
        }
        auto spatial_profile = _region_spatial_profiles.at(spatial_region_id);
        ulock.unlock();

        // Get file ids in spatial profile configurations
        std::vector<int> spatial_file_ids;
        if (file_id > ALL_FILES) {
            spatial_file_ids.push_back(file_id);
        } else {
            spatial_file_ids = spatial_profile->GetConfigFileIds(file_id);
            if (spatial_file_ids.empty()) {
                continue;
            }
        }

        for (int spatial_file_id : spatial_file_ids) {
            // Get statistics for specific region and file ids (input ids may be ALL)
            if (!RegionFileIdsValid(spatial_region_id, spatial_file_id)) {
                continue;
            }

            auto frame = _frames.at(spatial_file_id);
            int z = frame->CurrentZ();

            std::vector<CARTA::SetSpatialRequirements_SpatialConfig> spatial_configs;
            if (!spatial_profile->GetConfigurations(spatial_file_id, spatial_configs)) {
                continue;
            }

            auto region = GetRegion(spatial_region_id);
            auto region_type = region->GetRegionState().type;
            if (region_type == CARTA::POINT) {
                StokesSource stokes_source(frame->CurrentStokes(), AxisRange(z));
                auto lc_region = region->GetLCRegion(
                    spatial_file_id, frame->CoordinateSystem(stokes_source), frame->ImageShape(stokes_source), stokes_source);
                std::vector<CARTA::SpatialProfileData> spatial_profile_messages;

                if (spatial_profile->GetPointSpatialProfile(spatial_file_id, frame, spatial_configs, lc_region, spatial_profile_messages)) {
                    // Use callback to return each profile individually.
                    for (auto& profile_message : spatial_profile_messages) {
                        cb(profile_message);
                    }
                    success = true;
                }
            } else {
                for (auto& spatial_config : spatial_configs) {
                    if (!RegionFileIdsValid(spatial_region_id, spatial_file_id)) {
                        spdlog::info("Region {} spatial profile was cancelled, ids no longer valid.", spatial_region_id);
                        break;
                    }

                    int stokes_index(0);
                    if (!frame->GetStokesTypeIndex(spatial_config.coordinate(), stokes_index)) {
                        continue; // invalid image/computed Stokes
                    }

                    bool cancelled;
                    std::string message;
                    CARTA::SpatialProfileData spatial_profile_message;

                    if (!spatial_profile->GetLineSpatialProfile(
                            spatial_file_id, frame, region, stokes_index, z, spatial_config, cancelled, message, spatial_profile_message)) {
                        if (cancelled) {
                            spdlog::info("Region {} spatial profile was cancelled.", spatial_region_id);
                        } else {
                            spdlog::error("Line region {} spatial profile failed: {}", spatial_region_id, message);
                        }
                    } else {
                        // Use callback to return each profile individually.
                        cb(spatial_profile_message);
                        success = true;
                    }
                }
            }
        }

        if (_region_spatial_profiles.empty()) {
            break;
        }
    }
    return success;
}

bool RegionHandler::IsPointRegion(int region_id) {
    // Analytic region, not annotation
    if (RegionSet(region_id, true)) {
        return GetRegion(region_id)->IsPoint() && !GetRegion(region_id)->IsAnnotation();
    }
    return false;
}

bool RegionHandler::IsLineRegion(int region_id) {
    // Analytic region, not annotation
    if (RegionSet(region_id, true)) {
        return GetRegion(region_id)->IsLineType() && !GetRegion(region_id)->IsAnnotation();
    }
    return false;
}

bool RegionHandler::IsClosedRegion(int region_id) {
    // Analytic region, not annotation
    if (RegionSet(region_id, true)) {
        auto type = GetRegion(region_id)->GetRegionState().type;
        return (type == CARTA::RegionType::RECTANGLE) || (type == CARTA::RegionType::ELLIPSE) || (type == CARTA::RegionType::POLYGON);
    }
    return false;
}

bool RegionHandler::GetLineProfiles(int file_id, int region_id, int width, const AxisRange& z_range, int stokes_index,
    const std::string& coordinate, std::function<void(float)>& progress_callback, casacore::Matrix<float>& profiles,
    casacore::Quantity& increment, bool& cancelled, std::string& message, bool reverse) {
    // Generate box regions to approximate a line with a width (pixels), and get mean of each box (per z else current z).
    // Input parameters: file_id, region_id, width, z_range. z_range must be valid channel numbers, not CURRENT_Z or ALL_Z.
    // Calls progress_callback after each profile.
    // Return parameters: increment (angular spacing of boxes, in arcsec), per-region profiles, cancelled, message.
    // Returns whether profiles completed.
    if (width < 1 || width > 20) {
        message = fmt::format("Invalid averaging width: {}.", width);
        spdlog::error(message);
        return false;
    }

    if (!RegionSet(region_id, true)) {
        return false;
    }

    auto line_region = GetRegion(region_id);
    std::shared_lock region_lock(line_region->GetActiveTaskMutex());
    auto line_region_state = line_region->GetRegionState();
    auto line_coord_sys = line_region->CoordinateSystem();
    region_lock.unlock();

    if (CancelLineProfiles(region_id, file_id, line_region_state)) {
        cancelled = true;
        return false;
    }

    AxisRange spectral_range(z_range);
    float progress(0.0);

    // Get line approximated as series of box regions (returned as RegionState vector) and increment between them.
    LineBoxRegions line_box_regions;
    std::vector<RegionState> box_regions;

    if (line_box_regions.GetLineBoxRegions(line_region_state, line_coord_sys, width, increment, box_regions, message)) {
        auto t_start = std::chrono::high_resolution_clock::now();
        auto num_profiles = box_regions.size();
        for (size_t iprofile = 0; iprofile < num_profiles; ++iprofile) {
            // Frame/region closing, or line changed
            if (CancelLineProfiles(region_id, file_id, line_region_state)) {
                cancelled = true;
                profiles.resize();
                return false;
            }

            // Check if user canceled
            if (_stop_pv[file_id]) {
                spdlog::debug("Stopping line profiles: PV generator cancelled");
                cancelled = true;
                profiles.resize();
                return false;
            }

            // Get mean profile for requested file_id and log number of pixels in region
            double num_pixels(0.0);
            casacore::Vector<float> region_profile =
                GetTemporaryRegionProfile(file_id, box_regions[iprofile], line_coord_sys, z_range, stokes_index, num_pixels);
            // spdlog::debug(
            //     "File {} region {} line profile {} of {} max num pixels={}", file_id, region_id, iprofile, num_profiles, num_pixels);

            if (profiles.empty()) {
                if (reverse) {
                    profiles.resize(casacore::IPosition(2, region_profile.size(), num_profiles));
                } else {
                    profiles.resize(casacore::IPosition(2, num_profiles, region_profile.size()));
                }
            }

            if (reverse) {
                profiles.column(iprofile) = region_profile;
            } else {
                profiles.row(iprofile) = region_profile;
            }

            progress = float(iprofile + 1) / float(num_profiles);

            // Update progress if time interval elapsed
            auto t_end = std::chrono::high_resolution_clock::now();
            auto dt = std::chrono::duration<double, std::milli>(t_end - t_start).count();

            if ((dt > LINE_PROFILE_PROGRESS_INTERVAL) || (progress >= 1.0)) {
                t_start = t_end;
                progress_callback(progress);
            }
        }
    }

    return (!cancelled) && (progress >= 1.0) && !allEQ(profiles, FLOAT_NAN);
}

bool RegionHandler::CancelLineProfiles(int region_id, int file_id, RegionState& region_state) {
    // Cancel if region or frame is closing or line moved
    bool cancel = !RegionFileIdsValid(region_id, file_id);

    if (!cancel) {
        auto region = GetRegion(region_id);
        cancel = !region || (region->GetRegionState() != region_state);
    }

    if (cancel) {
        spdlog::debug("Cancel line profiles: region/file closed or changed");
    }

    return cancel;
}

casacore::Vector<float> RegionHandler::GetTemporaryRegionProfile(int file_id, RegionState& region_state,
    std::shared_ptr<casacore::CoordinateSystem> reference_csys, const AxisRange& z_range, int stokes_index, double& num_pixels) {
    // Initialize return values
    auto profile_size = z_range.to - z_range.from + 1;
    casacore::Vector<float> profile(profile_size, FLOAT_NAN);
    num_pixels = 0.0;

    if (!region_state.RegionDefined()) {
        return profile;
    }

    std::lock_guard<std::mutex> guard(_line_profile_mutex);
    // Set temporary region
    int temp_region_id(TEMP_REGION_ID);
    SetRegion(temp_region_id, region_state, reference_csys);
    if (!RegionSet(temp_region_id, true)) {
        return profile;
    }

    // Set temp region spectral requirements
    std::vector<CARTA::StatsType> required_stats = {CARTA::StatsType::NumPixels, CARTA::StatsType::Mean};
    ConfigId config_id(file_id, temp_region_id);
    std::string coordinate("z"); // current stokes
    SpectralConfig spectral_config(coordinate, required_stats);
    RegionSpectralConfig region_config;
    region_config.configs.push_back(spectral_config);
    std::unique_lock<std::mutex> ulock(_spectral_mutex);
    _spectral_req[config_id] = region_config;
    ulock.unlock();

    // Check cancel: currently temp per-z profile is only for PV image generator
    if (_stop_pv[file_id]) {
        RemoveRegion(temp_region_id);
        return profile;
    }

    // Get region spectral profiles converted to file_id image if necessary
    bool report_error(false);
    GetRegionSpectralData(temp_region_id, file_id, z_range, coordinate, stokes_index, required_stats, report_error,
        [&](std::map<CARTA::StatsType, std::vector<double>> results, float progress) {
            // Callback only sets return values when progress is complete.
            if (progress == 1.0) {
                // Get mean spectral profile and max NumPixels for small region.
                auto npix_per_chan = results[CARTA::StatsType::NumPixels];
                num_pixels = *max_element(npix_per_chan.begin(), npix_per_chan.end());
                profile = casacore::Vector<float>(results[CARTA::StatsType::Mean]); // TODO: use double for profile
            }
        });

    // Remove temporary region
    RemoveRegion(temp_region_id);

    return profile;
}

void RegionHandler::GetStokesPtotal(
    const ProfilesMap& profiles_q, const ProfilesMap& profiles_u, const ProfilesMap& profiles_v, ProfilesMap& profiles_ptotal) {
    auto calc_step1 = [&](double q, double u) { return (std::pow(q, 2) + std::pow(u, 2)); };
    auto calc_step2 = [&](double v, double step1) { return std::sqrt(step1 + std::pow(v, 2)); };

    CombineStokes(profiles_ptotal, profiles_q, profiles_u, calc_step1);
    CombineStokes(profiles_ptotal, profiles_v, calc_step2);
}

void RegionHandler::GetStokesPftotal(const ProfilesMap& profiles_i, const ProfilesMap& profiles_q, const ProfilesMap& profiles_u,
    const ProfilesMap& profiles_v, ProfilesMap& profiles_pftotal) {
    auto calc_step1 = [&](double q, double u) { return (std::pow(q, 2) + std::pow(u, 2)); };
    auto calc_step2 = [&](double v, double step1) { return std::sqrt(step1 + std::pow(v, 2)); };
    auto calc_step3 = [&](double i, double step2) { return 100.0 * (step2 / i); };

    CombineStokes(profiles_pftotal, profiles_q, profiles_u, calc_step1);
    CombineStokes(profiles_pftotal, profiles_v, calc_step2);
    CombineStokes(profiles_pftotal, profiles_i, calc_step3);
}

void RegionHandler::GetStokesPlinear(const ProfilesMap& profiles_q, const ProfilesMap& profiles_u, ProfilesMap& profiles_plinear) {
    auto calc_pi = [&](double q, double u) { return std::sqrt(std::pow(q, 2) + std::pow(u, 2)); };

    CombineStokes(profiles_plinear, profiles_q, profiles_u, calc_pi);
}

void RegionHandler::GetStokesPflinear(
    const ProfilesMap& profiles_i, const ProfilesMap& profiles_q, const ProfilesMap& profiles_u, ProfilesMap& profiles_pflinear) {
    auto calc_pi = [&](double q, double u) { return std::sqrt(std::pow(q, 2) + std::pow(u, 2)); };
    auto calc_fpi = [&](double i, double pi) { return (IsValid(i, pi) ? 100.0 * (pi / i) : DOUBLE_NAN); };

    CombineStokes(profiles_pflinear, profiles_q, profiles_u, calc_pi);
    CombineStokes(profiles_pflinear, profiles_i, calc_fpi);
}

void RegionHandler::GetStokesPangle(const ProfilesMap& profiles_q, const ProfilesMap& profiles_u, ProfilesMap& profiles_pangle) {
    auto calc_pa = [&](double q, double u) { return (180.0 / casacore::C::pi) * atan2(u, q) / 2; };

    CombineStokes(profiles_pangle, profiles_q, profiles_u, calc_pa);
}

void RegionHandler::CombineStokes(ProfilesMap& profiles_out, const ProfilesMap& profiles_q, const ProfilesMap& profiles_u,
    const std::function<double(double, double)>& func) {
    auto func_if_valid = [&](double a, double b) { return (IsValid(a, b) ? func(a, b) : DOUBLE_NAN); };

    for (auto stats_q : profiles_q) {
        for (auto stats_u : profiles_u) {
            if (stats_q.first == stats_u.first) {
                std::vector<double>& results = profiles_out[stats_q.first];
                results.resize(stats_q.second.size());
                std::transform(stats_q.second.begin(), stats_q.second.end(), stats_u.second.begin(), results.begin(), func_if_valid);
            }
        }
    }
}

void RegionHandler::CombineStokes(
    ProfilesMap& profiles_out, const ProfilesMap& profiles_other, const std::function<double(double, double)>& func) {
    auto func_if_valid = [&](double a, double b) { return (IsValid(a, b) ? func(a, b) : DOUBLE_NAN); };

    for (auto stats_out : profiles_out) {
        for (auto stats_other : profiles_other) {
            if (stats_out.first == stats_other.first) {
                std::vector<double>& results = profiles_out[stats_out.first];
                std::transform(
                    stats_other.second.begin(), stats_other.second.end(), stats_out.second.begin(), results.begin(), func_if_valid);
            }
        }
    }
}

bool RegionHandler::IsValid(double a, double b) {
    return (!std::isnan(a) && !std::isnan(b));
}

bool RegionHandler::GetComputedStokesProfiles(
    ProfilesMap& profiles, int stokes_index, const std::function<bool(ProfilesMap&, std::string)>& get_profiles_data) {
    ProfilesMap profile_i, profile_q, profile_u, profile_v;
    if (stokes_index == CARTA::PolarizationType::Ptotal) {
        if (!get_profiles_data(profile_q, "Qz") || !get_profiles_data(profile_u, "Uz") || !get_profiles_data(profile_v, "Vz")) {
            return false;
        }
        GetStokesPtotal(profile_q, profile_u, profile_v, profiles);
    } else if (stokes_index == CARTA::PolarizationType::PFtotal) {
        if (!get_profiles_data(profile_i, "Iz") || !get_profiles_data(profile_q, "Qz") || !get_profiles_data(profile_u, "Uz") ||
            !get_profiles_data(profile_v, "Vz")) {
            return false;
        }
        GetStokesPftotal(profile_i, profile_q, profile_u, profile_v, profiles);
    } else if (stokes_index == CARTA::PolarizationType::Plinear) {
        if (!get_profiles_data(profile_q, "Qz") || !get_profiles_data(profile_u, "Uz")) {
            return false;
        }
        GetStokesPlinear(profile_q, profile_u, profiles);
    } else if (stokes_index == CARTA::PolarizationType::PFlinear) {
        if (!get_profiles_data(profile_i, "Iz") || !get_profiles_data(profile_q, "Qz") || !get_profiles_data(profile_u, "Uz")) {
            return false;
        }
        GetStokesPflinear(profile_i, profile_q, profile_u, profiles);
    } else if (stokes_index == CARTA::PolarizationType::Pangle) {
        if (!get_profiles_data(profile_q, "Qz") || !get_profiles_data(profile_u, "Uz")) {
            return false;
        }
        GetStokesPangle(profile_q, profile_u, profiles);
    }
    return true;
}

} // namespace carta
