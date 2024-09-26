/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// RegionDataHandler.cc: handle requirements and data streams for regions

#include "RegionHandler.h"

#include <chrono>

#include <casacore/casa/math.h>
#include <casacore/lattices/LRegions/LCBox.h>
#include <casacore/lattices/LRegions/LCExtension.h>
#include <casacore/lattices/LRegions/LCIntersection.h>

#include "CrtfImportExport.h"
#include "Ds9ImportExport.h"
#include "ImageData/FileLoader.h"
#include "ImageStats/StatsCalculator.h"
#include "LineBoxRegions.h"
#include "Logger/Logger.h"
#include "Timer/Timer.h"
#include "Util/File.h"
#include "Util/Image.h"

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

    if ((region_id > 0) && RegionSet(region_id)) {
        // Check for id > 0 so do not update temp region
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
    const casacore::IPosition shape = frame->ImageShape();
    std::unique_ptr<RegionImportExport> importer;

    try {
        switch (region_file_type) {
            case CARTA::FileType::CRTF:
                importer.reset(new CrtfImportExport(csys, shape, frame->StokesAxis(), file_id, region_file, file_is_filename));
                break;
            case CARTA::FileType::DS9_REG:
                importer.reset(new Ds9ImportExport(csys, shape, file_id, region_file, file_is_filename));
                break;
            default:
                break;
        }
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

    // Get regions from parser or error message
    std::string error;
    std::vector<RegionProperties> region_list = importer->GetImportedRegions(error);
    if (region_list.empty()) {
        import_ack.set_success(false);
        import_ack.set_message(error);
        return;
    }

    // Set reference file pointer
    _frames[file_id] = frame;

    // Set Regions from RegionState list and complete message
    import_ack.set_success(true);
    import_ack.set_message(error);
    int region_id = GetNextRegionId();
    auto region_info_map = import_ack.mutable_regions();
    auto region_style_map = import_ack.mutable_region_styles();
    for (auto& imported_region : region_list) {
        auto region_state = imported_region.state;
        auto region_style = imported_region.style;

        auto region_csys = frame->CoordinateSystem();
        auto region = std::shared_ptr<Region>(new Region(region_state, region_csys));

        if (region && region->IsValid()) {
            std::unique_lock<std::mutex> region_lock(_region_mutex);
            _regions[region_id] = std::move(region);
            region_lock.unlock();

            // Set CARTA::RegionInfo
            CARTA::RegionInfo region_info;
            region_info.set_region_type(region_state.type);
            *region_info.mutable_control_points() = {region_state.control_points.begin(), region_state.control_points.end()};
            region_info.set_rotation(region_state.rotation);

            // Add info and style to import_ack; increment region id for next region
            (*region_info_map)[region_id] = region_info;
            (*region_style_map)[region_id++] = region_style;
        }
    }
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

    // Check ability to create export file if filename given
    std::string message;
    if (!filename.empty()) {
        casacore::File export_file(filename);
        if (export_file.exists()) {
            if (export_file.isDirectory()) {
                message = "Export region failed: cannot overwrite existing directory.";
            } else if (!export_file.isRegular()) {
                message = "Export region failed: existing path is not a file.";
            } else if (!overwrite) {
                message = "Export region failed: cannot overwrite existing file.";
                export_ack.set_overwrite_confirmation_required(true);
            } else if (!export_file.isWritable()) {
                message = "Export region failed: cannot overwrite read-only file.";
            }
        } else if (!export_file.canCreate()) {
            message = "Export region failed: cannot create file.";
        }

        if (!message.empty()) {
            export_ack.set_success(false);
            export_ack.set_message(message);
            return;
        }
    }

    bool export_pixel_coord(coord_type == CARTA::CoordinateType::PIXEL);
    auto output_csys = frame->CoordinateSystem();
    if (!export_pixel_coord && !output_csys->hasDirectionCoordinate()) {
        // Export fails, cannot convert to world coordinates
        export_ack.set_success(false);
        export_ack.set_message("Cannot export regions in world coordinates for linear coordinate system.");
        return;
    }

    const casacore::IPosition output_shape = frame->ImageShape();
    std::unique_ptr<RegionImportExport> exporter;
    switch (region_file_type) {
        case CARTA::FileType::CRTF:
            exporter = std::unique_ptr<RegionImportExport>(new CrtfImportExport(output_csys, output_shape, frame->StokesAxis()));
            break;
        case CARTA::FileType::DS9_REG:
            exporter = std::unique_ptr<RegionImportExport>(new Ds9ImportExport(output_csys, output_shape, export_pixel_coord));
            break;
        default:
            break;
    }

    for (auto& region_id_style : region_styles) {
        auto region_id = region_id_style.first;
        auto region_style = region_id_style.second;

        if (RegionSet(region_id)) {
            bool region_added(false);
            auto region = GetRegion(region_id);
            auto region_state = region->GetRegionState();

            if ((region_state.reference_file_id == file_id) && export_pixel_coord) {
                // Use RegionState control points with reference file id for pixel export
                region_added = exporter->AddExportRegion(region_state, region_style);
            } else {
                try {
                    // Use Record containing pixel coords of region converted to output image
                    casacore::TableRecord region_record = region->GetImageRegionRecord(file_id, output_csys, output_shape);
                    if (!region_record.empty()) {
                        region_added = exporter->AddExportRegion(region_state, region_style, region_record, export_pixel_coord);
                    }
                } catch (const casacore::AipsError& err) {
                    spdlog::error("Export region record failed: {}", err.getMesg());
                }
            }

            if (!region_added) {
                std::string region_error = fmt::format("Export region {} in image {} failed.\n", region_id, file_id);
                message.append(region_error);
            }
        } else {
            std::string region_error = fmt::format("Region {} not found for export.\n", region_id);
            message.append(region_error);
        }
    }

    bool success(false);
    if (filename.empty()) {
        // Return contents
        std::vector<std::string> line_contents;
        success = exporter->ExportRegions(line_contents, message);
        if (success) {
            *export_ack.mutable_contents() = {line_contents.begin(), line_contents.end()};
        }
    } else {
        // Write to file
        success = exporter->ExportRegions(filename, message);
    }

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
        StopPvCalc(file_id);
        _frames.erase(file_id);
        RemoveFileRequirementsCache(file_id);
    }
}

// ********************************************************************
// Region requirements handling

bool RegionHandler::SetHistogramRequirements(
    int region_id, int file_id, std::shared_ptr<Frame> frame, const std::vector<CARTA::HistogramConfig>& configs) {
    // Set histogram requirements for closed region

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

    // Make HistogramConfig vector of requirements
    std::vector<HistogramConfig> input_configs;
    for (const auto& config : configs) {
        HistogramConfig hist_config(config);
        input_configs.push_back(hist_config);
    }

    // Set requirements
    ConfigId config_id(file_id, region_id);
    _histogram_req[config_id].configs = input_configs;
    return true;
}

bool RegionHandler::SetSpatialRequirements(int region_id, int file_id, std::shared_ptr<Frame> frame,
    const std::vector<CARTA::SetSpatialRequirements_SpatialConfig>& spatial_profiles) {
    // Set spatial requirements for point or line

    if (spatial_profiles.empty() && !RegionSet(region_id)) {
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

    // Set spatial profile requirements for given region and file
    ConfigId config_id(file_id, region_id);

    std::unique_lock<std::mutex> ulock(_spatial_mutex);
    if (_spatial_req.count(config_id)) {
        _spatial_req[config_id].clear();
    }

    // Set new requirements for this file/region
    _spatial_req[config_id] = spatial_profiles;
    ulock.unlock();

    return true;
}

bool RegionHandler::HasSpatialRequirements(int region_id, int file_id, const std::string& coordinate, int width) {
    // Search _spatial_req for given file, region, stokes, and width.
    // Used to check for cancellation.
    ConfigId config_id(file_id, region_id);
    std::vector<CARTA::SetSpatialRequirements_SpatialConfig> spatial_configs;
    std::unique_lock<std::mutex> ulock(_spatial_mutex);
    spatial_configs.insert(spatial_configs.begin(), _spatial_req[config_id].begin(), _spatial_req[config_id].end());
    ulock.unlock();

    bool has_req(false);
    for (auto& config : spatial_configs) {
        if ((config.coordinate() == coordinate) && (config.width() == width)) {
            has_req = true;
            break;
        }
    }

    if (!has_req) {
        spdlog::debug("Spatial requirements for region {} file {} coordinate {} width {} removed", region_id, file_id, coordinate, width);
    }

    return has_req;
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
    int region_id, int file_id, std::shared_ptr<Frame> frame, const std::vector<CARTA::SetStatsRequirements_StatsConfig>& stats_configs) {
    // Set stats data requirements for closed region

    if (stats_configs.empty() && !RegionSet(region_id)) {
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

    // Set requirements
    ConfigId config_id(file_id, region_id);
    _stats_req[config_id].stats_configs = stats_configs;
    return true;
}

void RegionHandler::RemoveRegionRequirementsCache(int region_id) {
    // Clear requirements and cache for a specific region or for all regions when closed
    if (region_id == ALL_REGIONS) {
        _histogram_req.clear();
        _stats_req.clear();

        std::unique_lock<std::mutex> spectral_lock(_spectral_mutex);
        _spectral_req.clear();
        spectral_lock.unlock();

        std::unique_lock<std::mutex> spatial_lock(_spatial_mutex);
        _spatial_req.clear();
        spatial_lock.unlock();

        _histogram_cache.clear();
        _spectral_cache.clear();
        _stats_cache.clear();

        std::unique_lock pv_cut_lock(_pv_cut_mutex);
        _pv_preview_cuts.clear();
        std::unique_lock pv_cube_lock(_pv_cube_mutex);
        _pv_preview_cubes.clear();
    } else {
        // Iterate through requirements and remove those for given region_id
        for (auto it = _histogram_req.begin(); it != _histogram_req.end();) {
            if ((*it).first.region_id == region_id) {
                it = _histogram_req.erase(it);
            } else {
                ++it;
            }
        }

        std::unique_lock<std::mutex> spectral_lock(_spectral_mutex);
        for (auto it = _spectral_req.begin(); it != _spectral_req.end();) {
            if ((*it).first.region_id == region_id) {
                it = _spectral_req.erase(it);
            } else {
                ++it;
            }
        }
        spectral_lock.unlock();

        for (auto it = _stats_req.begin(); it != _stats_req.end();) {
            if ((*it).first.region_id == region_id) {
                it = _stats_req.erase(it);
            } else {
                ++it;
            }
        }

        std::unique_lock<std::mutex> spatial_lock(_spatial_mutex);
        for (auto it = _spatial_req.begin(); it != _spatial_req.end();) {
            if ((*it).first.region_id == region_id) {
                it = _spatial_req.erase(it);
            } else {
                ++it;
            }
        }
        spatial_lock.unlock();

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

        if (region_id > 0) {
            // Needed only for pv cut region in source image.
            // Do not attempt to get lock for removing temporary box region.
            std::unique_lock pv_cut_lock(_pv_cut_mutex);
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
        _histogram_req.clear();
        _stats_req.clear();
        std::unique_lock<std::mutex> spectral_lock(_spectral_mutex);
        _spectral_req.clear();
        spectral_lock.unlock();
        std::unique_lock<std::mutex> spatial_lock(_spatial_mutex);
        _spatial_req.clear();
        spatial_lock.unlock();

        _histogram_cache.clear();
        _spectral_cache.clear();
        _stats_cache.clear();

        std::unique_lock pv_cut_lock(_pv_cut_mutex);
        _pv_preview_cuts.clear();
        std::unique_lock pv_cube_lock(_pv_cube_mutex);
        _pv_preview_cubes.clear();
    } else {
        // Iterate through requirements and remove those for given file_id
        for (auto it = _histogram_req.begin(); it != _histogram_req.end();) {
            if ((*it).first.file_id == file_id) {
                it = _histogram_req.erase(it);
            } else {
                ++it;
            }
        }

        std::unique_lock<std::mutex> spectral_lock(_spectral_mutex);
        for (auto it = _spectral_req.begin(); it != _spectral_req.end();) {
            if ((*it).first.file_id == file_id) {
                it = _spectral_req.erase(it);
            } else {
                ++it;
            }
        }
        spectral_lock.unlock();

        for (auto it = _stats_req.begin(); it != _stats_req.end();) {
            if ((*it).first.file_id == file_id) {
                it = _stats_req.erase(it);
            } else {
                ++it;
            }
        }

        std::unique_lock<std::mutex> spatial_lock(_spatial_mutex);
        for (auto it = _spatial_req.begin(); it != _spatial_req.end();) {
            if ((*it).first.file_id == file_id) {
                it = _spatial_req.erase(it);
            } else {
                ++it;
            }
        }
        spatial_lock.unlock();

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

        std::unique_lock pv_cut_lock(_pv_cut_mutex);
        for (auto it = _pv_preview_cuts.begin(); it != _pv_preview_cuts.end();) {
            if ((*it).second->HasPreviewFileRegionIds(file_id, ALL_REGIONS)) {
                it = _pv_preview_cuts.erase(it);
            } else {
                ++it;
            }
        }
        pv_cut_lock.unlock();

        std::unique_lock pv_cube_lock(_pv_cube_mutex);
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

std::shared_ptr<casacore::LCRegion> RegionHandler::ApplyRegionToFile(
    int region_id, int file_id, const StokesSource& stokes_source, bool report_error) {
    // Returns 2D region with no extension; nullptr if outside image or not closed region
    // Go through Frame for image mutex
    if (!RegionFileIdsValid(region_id, file_id, true)) {
        return nullptr;
    }

    if (!IsClosedRegion(region_id) && !IsPointRegion(region_id)) {
        return nullptr;
    }

    auto region = _regions.at(region_id);
    return _frames.at(file_id)->GetImageRegion(file_id, region, stokes_source, report_error);
}

bool RegionHandler::ApplyRegionToFile(int region_id, int file_id, const AxisRange& z_range, int stokes,
    std::shared_ptr<casacore::LCRegion> lc_region, StokesRegion& stokes_region) {
    // LCRegion applied to image then extended by z-range and stokes index.
    // LCRegion can be supplied, or will be set using region and file IDs.
    // Returns StokesRegion struct with StokesSource and 3D ImageRegion.
    if (!RegionFileIdsValid(region_id, file_id, true)) {
        return false;
    }

    try {
        StokesSource stokes_source(stokes, z_range);
        stokes_region.stokes_source = stokes_source;
        auto applied_region = lc_region;
        if (!applied_region) {
            applied_region = ApplyRegionToFile(region_id, file_id, stokes_source);
        }

        // Check applied region
        if (!applied_region) {
            return false;
        }

        casacore::IPosition image_shape(_frames.at(file_id)->ImageShape(stokes_source));

        // Create LCBox with z range and stokes using a slicer
        casacore::Slicer z_stokes_slicer = _frames.at(file_id)->GetImageSlicer(z_range, stokes).slicer;

        // Set returned region
        // Combine applied region with z/stokes box
        if (applied_region->shape().size() == image_shape.size()) {
            // Intersection combines applied_region xy limits and box z/stokes limits
            casacore::LCBox z_stokes_box(z_stokes_slicer, image_shape);
            casacore::LCIntersection final_region(*applied_region, z_stokes_box);
            stokes_region.image_region = casacore::ImageRegion(final_region);
        } else {
            // Extension extends applied_region in xy axes by z/stokes axes only
            // Remove xy axes from z/stokes box
            casacore::IPosition remove_xy(2, 0, 1);
            z_stokes_slicer =
                casacore::Slicer(z_stokes_slicer.start().removeAxes(remove_xy), z_stokes_slicer.length().removeAxes(remove_xy));
            casacore::LCBox z_stokes_box(z_stokes_slicer, image_shape.removeAxes(remove_xy));

            casacore::IPosition extend_axes = casacore::IPosition::makeAxisPath(image_shape.size()).removeAxes(remove_xy);
            casacore::LCExtension final_region(*applied_region, extend_axes, z_stokes_box);
            stokes_region.image_region = casacore::ImageRegion(final_region);
        }

        return true;
    } catch (const casacore::AipsError& err) {
        spdlog::error("Error applying region {} to file {}: {}", region_id, file_id, err.getMesg());
    } catch (std::out_of_range& range_error) {
        spdlog::error("Cannot apply region {} to closed file {}", region_id, file_id);
    }

    return false;
}

// ********************************************************************
// Generated images

bool RegionHandler::CalculateMoments(int file_id, int region_id, const std::shared_ptr<Frame>& frame,
    GeneratorProgressCallback progress_callback, const CARTA::MomentRequest& moment_request, CARTA::MomentResponse& moment_response,
    std::vector<GeneratedImage>& collapse_results) {
    StokesRegion stokes_region;
    std::shared_ptr<casacore::LCRegion> lc_region;
    int z_min(moment_request.spectral_range().min());
    int z_max(moment_request.spectral_range().max());

    // Do calculations
    if (ApplyRegionToFile(region_id, file_id, AxisRange(z_min, z_max), frame->CurrentStokes(), lc_region, stokes_region)) {
        frame->CalculateMoments(file_id, progress_callback, stokes_region, moment_request, moment_response, collapse_results,
            _regions.at(region_id)->GetRegionState());
    }
    return !collapse_results.empty();
}

bool RegionHandler::CalculateRender3DData(const CARTA::Render3DRequest& render3d_request, GeneratorProgressCallback progress_callback, CARTA::Render3DResponse render3d_response, CARTA::Render3DData render3d_data) {
    // Unpack request message and send it along
    int file_id(render3d_request.file_id());
    int region_id(render3d_request.region_id());
    bool keep(render3d_request.keep());
    AxisRange spectral_range;
    if (render3d_request.has_spectral_range()) {
        spectral_range = AxisRange(render3d_request.spectral_range().min(), render3d_request.spectral_range().max());
    } else {
        spectral_range = AxisRange(0, frame->Depth() - 1);
    }
    CARTA::ImageBounds image_bounds;
    if (render3d_request.has_image_bounds()) {
    image_bounds = CARTA::ImageBounds(render3d_request.image_bounds().x_min(), render3d_request.image_bounds().x_max(), render3d_request.image_bounds().y_min(), render3d_request.image_bounds().y_max());
    } else {
        image_bounds = CARTA::ImageBounds(0, frame->Width() - 1, 0, frame->Height() - 1);
    }
    render3d_response.set_success(false);
    render3d_response.set_cancel(false);

    // Checks for valid request:
    // 1. Region is set
    if (!RegionSet(region_id, true)) {
        render3d_response.set_message("3D Rendering requested for invalid region.");
        return false;
    }

    // 2. Region is line
    if (!IsClosedRegion(region_id)) {
        render3d_response.set_message("Region type not supported for 3D Rendering.");
        return false;
    }

    // 3. Image has spectral axis
    if (!frame->CoordinateSystem()->hasSpectralAxis()) {
        render3d_response.set_message("No spectral coordinate for generating 3D rendering.");
        return false;
    }

    // Set frame
    if (!FrameSet(file_id)) {
        _frames[file_id] = frame;
    }

    return CalculateRender3DData(file_id, region_id, spectral_range, image_bounds, keep, frame, progress_callback, render3d_response, render3d_data);
    
}

bool RegionHandler::CalculatePvImage(const CARTA::PvRequest& pv_request, std::shared_ptr<Frame>& frame,
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

        preview_region_state = _regions.at(preview_region_id)->GetRegionState();
    }

    // Save cut and cube settings for updates, including current pv cut region state
    RegionState region_state = GetRegion(region_id)->GetRegionState();
    auto stokes = frame->CurrentStokes();
    PreviewCutParameters cut_parameters(
        file_id, region_id, line_width, reverse, compression, image_quality, animation_quality, region_state.reference_file_id);
    PreviewCubeParameters cube_parameters(file_id, preview_region_id, spectral_range, rebin_xy, rebin_z, stokes, preview_region_state);

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

    auto frame_id = GetPvPreviewFrameId(preview_id);
    bool preview_frame_set = _frames.find(frame_id) != _frames.end();

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
                auto slicer = frame->GetImageSlicer(spectral_range, frame->CurrentStokes());
                if (!frame->GetSlicerSubImage(slicer, sub_image)) {
                    pv_response.set_message("Failed to set spectral range for preview cube.");
                    return false;
                }
                casacore::IPosition origin(2, 0, 0);
                preview_cube->SetPreviewRegionOrigin(origin);
            } else {
                // Apply preview region to source image (LCRegion) to get SubImage
                StokesSource stokes_source(stokes, spectral_range);
                std::shared_ptr<casacore::LCRegion> lc_region = ApplyRegionToFile(preview_region_id, file_id, stokes_source);
                if (!lc_region) {
                    pv_response.set_message("Failed to set preview region for preview cube.");
                    return false;
                }

                // Origin (blc) for setting pv cut in cube
                auto origin = lc_region->boundingBox().start();
                preview_cube->SetPreviewRegionOrigin(origin);

                // Apply LCRegion and spectral range to source image to get StokesRegion
                StokesRegion stokes_region;
                if (!ApplyRegionToFile(preview_region_id, file_id, spectral_range, stokes, lc_region, stokes_region)) {
                    pv_response.set_message("Failed to set preview region or spectral range for preview cube.");
                    return false;
                }

                // Apply StokesRegion to source image to get SubImage
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

        _frames[frame_id] = preview_frame;
    }
    spdlog::performance("PV preview cube and frame in {:.3f} ms", t.Elapsed().ms());

    bool quick_update(false);
    return CalculatePvPreviewImage(frame_id, preview_id, quick_update, preview_cut, preview_cube, progress_callback, pv_response, pv_image);
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

        // Get box region LCRegion and mask
        bool cancel(false);
        auto box_lc_region = ApplyRegionToFile(box_region_id, frame_id);

        if (!box_lc_region) {
            RemoveRegion(box_region_id);
            continue;
        }

        auto bounding_box = box_lc_region->boundingBox();
        auto box_mask = _regions.at(box_region_id)->GetImageRegionMask(frame_id);
        RemoveRegion(box_region_id);

        // Use PvPreviewCube to calculate profile with lcregion and mask
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
    int stokes(preview_cube->GetStokes());
    PvGenerator pv_generator;
    pv_generator.SetFileName(preview_id, preview_cube->GetSourceFileName(), true);

    if (pv_generator.GetPvImage(
            preview_frame, no_preview_data, data_shape, pos_axis_type, increment, start_channel, stokes, reverse, pv_image, error)) {
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

bool RegionHandler::CalculateRender3DData(int file_id, int region_id, CARTA::ImageBounds& image_bounds, AxisRange& spectral_range, bool keep, std::shared_ptr<Frame>& frame, GeneratorProgressCallback progress_callback, CARTA::Render3DResponse& render3d_response, CARTA::Render3DData& render3d_data) {
    render3d_response.set_success(false);
    render3d_response.set_cancel(false);

    // use if region is mandatory
    // auto region = GetRegion(region_id);
    // if (!region) {
    //     render3d_response.set_message("Render3D region not set");
    //     return false;
    // }

    casacore::Cube<float> render3d_cube;

    
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
    bool per_z(true), pv_success(false), cancelled(false);
    int stokes_index = frame->CurrentStokes();
    casacore::Quantity offset_increment;
    casacore::Matrix<float> pv_data;
    std::string message;
    if (spectral_range.to == ALL_Z) {
        spectral_range.to = frame->Depth() - 1;
    }

    if (GetLineProfiles(file_id, region_id, width, spectral_range, per_z, stokes_index, "", progress_callback, pv_data, offset_increment,
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
    int stokes = frame->CurrentStokes();
    StokesRegion stokes_region;
    std::shared_ptr<casacore::LCRegion> lc_region;

    if (!ApplyRegionToFile(region_id, file_id, z_range, stokes, lc_region, stokes_region)) {
        fitting_response.set_message("region is outside image or is not closed");
        fitting_response.set_success(false);
        return false;
    }

    bool success = false;
    success = frame->FitImage(fitting_request, fitting_response, model_image, residual_image, progress_callback, &stokes_region);

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

// ***** Fill histogram *****

bool RegionHandler::FillRegionHistogramData(
    std::function<void(CARTA::RegionHistogramData histogram_data)> region_histogram_callback, int region_id, int file_id) {
    // Fill histogram data for given region and file
    if (!RegionFileIdsValid(region_id, file_id, true)) {
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
                std::vector<CARTA::RegionHistogramData> histogram_messages;
                std::vector<HistogramConfig> histogram_configs = region_config.second.configs;
                if (GetRegionHistogramData(region_id, config_file_id, histogram_configs, histogram_messages)) {
                    for (const auto& histogram_message : histogram_messages) {
                        region_histogram_callback(histogram_message); // send histogram data with respect to stokes
                    }
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
                std::vector<CARTA::RegionHistogramData> histogram_messages;
                if (GetRegionHistogramData(config_region_id, file_id, histogram_configs, histogram_messages)) {
                    for (const auto& histogram_message : histogram_messages) {
                        region_histogram_callback(histogram_message); // send histogram data with respect to stokes
                    }
                    message_filled = true;
                }
            }
        }
    }
    return message_filled;
}

bool RegionHandler::GetRegionHistogramData(
    int region_id, int file_id, std::vector<HistogramConfig>& configs, std::vector<CARTA::RegionHistogramData>& histogram_messages) {
    // Fill stats message for given region, file
    Timer t;

    // Set channel range is the current channel
    int z(_frames.at(file_id)->CurrentZ());
    AxisRange z_range(z);

    // Stokes type to be determined in each of histogram configs
    int stokes;

    // Flags for calculations
    bool have_basic_stats(false);

    // Reuse the image region for each histogram
    StokesRegion stokes_region;
    std::shared_ptr<casacore::LCRegion> lc_region;

    // Reuse data with respect to stokes and stats for each histogram; results depend on num_bins
    std::unordered_map<int, std::vector<float>> data;
    BasicStats<float> stats;

    for (auto& hist_config : configs) {
        // check for cancel
        if (!RegionFileIdsValid(region_id, file_id)) {
            return false;
        }

        // Get stokes index
        if (!_frames.at(file_id)->GetStokesTypeIndex(hist_config.coordinate, stokes)) {
            continue;
        }

        // Set histogram fields
        auto histogram_message = Message::RegionHistogramData(file_id, region_id, z, stokes, 1.0, hist_config);

        // Get image region
        if (!ApplyRegionToFile(region_id, file_id, z_range, stokes, lc_region, stokes_region)) {
            // region outside image, send default histogram
            auto* default_histogram = histogram_message.mutable_histograms();
            std::vector<int> histogram_bins(1, 0);
            FillHistogram(default_histogram, 1, 0.0, 0.0, histogram_bins, NAN, NAN);
            continue;
        }

        // number of bins may be set or calculated
        int num_bins(hist_config.num_bins);
        if (num_bins == AUTO_BIN_SIZE) {
            casacore::IPosition region_shape = _frames.at(file_id)->GetRegionShape(stokes_region);
            num_bins = int(std::max(sqrt(region_shape(0) * region_shape(1)), 2.0));
        }

        // Key for cache
        CacheId cache_id = CacheId(file_id, region_id, stokes, z);

        // check cache
        if (_histogram_cache.count(cache_id)) {
            have_basic_stats = _histogram_cache[cache_id].GetBasicStats(stats);
            if (have_basic_stats) {
                // Set histogram bounds
                auto bounds = hist_config.GetBounds(stats);
                Histogram hist;
                if (_histogram_cache[cache_id].GetHistogram(num_bins, bounds, hist)) {
                    auto* histogram = histogram_message.mutable_histograms();
                    FillHistogram(histogram, stats, hist);

                    // Fill in the cached message
                    histogram_messages.emplace_back(histogram_message);
                    continue;
                }
            }
        }

        // Calculate stats and/or histograms, not in cache
        // Get data in region
        if (!data.count(stokes)) {
            if (!_frames.at(file_id)->GetRegionData(stokes_region, data[stokes])) {
                spdlog::error("Failed to get data in the region!");
                return false;
            }
        }

        // Calculate and cache stats
        if (!have_basic_stats) {
            CalcBasicStats(stats, data[stokes].data(), data[stokes].size());
            _histogram_cache[cache_id].SetBasicStats(stats);
            have_basic_stats = true;
        }

        // Set histogram bounds
        Bounds bounds = hist_config.GetBounds(stats);

        // Calculate and cache histogram for number of bins
        Histogram histo = CalcHistogram(num_bins, bounds, data[stokes].data(), data[stokes].size());
        _histogram_cache[cache_id].SetHistogram(num_bins, histo);

        // Complete Histogram submessage
        auto* histogram = histogram_message.mutable_histograms();
        FillHistogram(histogram, stats, histo);

        // Fill in the final result
        histogram_messages.emplace_back(histogram_message);
    }

    auto dt = t.Elapsed();
    spdlog::performance("Fill region histogram in {:.3f} ms at {:.3f} MPix/s", dt.ms(), (float)stats.num_pixels / dt.us());

    return true;
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
                        auto profile_message = Message::SpectralProfileData(
                            config_file_id, config_region_id, stokes_index, progress, coordinate, required_stats, results);
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

    std::shared_lock frame_lock(_frames.at(file_id)->GetActiveTaskMutex());
    auto region = GetRegion(region_id);
    std::shared_lock region_lock(region->GetActiveTaskMutex());

    // Initialize results map for requested stats to NaN, progress to zero
    size_t profile_end = z_range.to;
    size_t profile_size = z_range.to - z_range.from + 1;
    std::vector<double> init_spectral(profile_size, nan(""));
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
    auto lc_region = ApplyRegionToFile(region_id, file_id, StokesSource(), report_error);
    if (!lc_region) {
        // region outside image, send NaNs
        progress = 1.0;
        partial_results_callback(results, progress);
        return true;
    }

    // Get initial region info to cancel profile if it changes
    RegionState initial_region_state = region->GetRegionState();

    // Use loader swizzled data for efficiency
    if (_frames.at(file_id)->UseLoaderSpectralData(lc_region->shape())) {
        // Use cursor spectral profile for point region
        if (initial_region_state.type == CARTA::RegionType::POINT) {
            casacore::IPosition origin = lc_region->boundingBox().start();
            auto point = Message::Point(origin(0), origin(1));

            auto get_stokes_profiles_data = [&](ProfilesMap& tmp_results, int tmp_stokes) {
                std::vector<float> tmp_profile;
                if (!_frames.at(file_id)->GetLoaderPointSpectralData(tmp_profile, tmp_stokes, point)) {
                    return false;
                }
                // Set results; there is only one required stat for point
                std::vector<double> tmp_data(tmp_profile.begin(), tmp_profile.end());
                tmp_results[required_stats[0]] = tmp_data;
                return true;
            };

            auto get_profiles_data = [&](ProfilesMap& tmp_results, std::string tmp_coordinate) {
                int tmp_stokes;
                return (_frames.at(file_id)->GetStokesTypeIndex(tmp_coordinate, tmp_stokes) &&
                        get_stokes_profiles_data(tmp_results, tmp_stokes));
            };

            if (IsComputedStokes(stokes_index)) { // For computed stokes
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
                if (use_current_stokes && (stokes_index != _frames.at(file_id)->CurrentStokes())) {
                    return false;
                }
                if (!HasSpectralRequirements(region_id, file_id, coordinate, required_stats)) {
                    return false;
                }

                // Get partial profile
                auto get_profiles_data = [&](ProfilesMap& tmp_results, std::string tmp_coordinate) {
                    int tmp_stokes;
                    return (
                        _frames.at(file_id)->GetStokesTypeIndex(tmp_coordinate, tmp_stokes) &&
                        _frames.at(file_id)->GetLoaderSpectralData(region_id, z_range, tmp_stokes, mask, xy_origin, tmp_results, progress));
                };

                ProfilesMap partial_profiles;
                if (IsComputedStokes(stokes_index)) { // For computed stokes
                    if (!GetComputedStokesProfiles(partial_profiles, stokes_index, get_profiles_data)) {
                        return false;
                    }
                } else { // For regular stokes I, Q, U, or V
                    if (!_frames.at(file_id)->GetLoaderSpectralData(
                            region_id, z_range, stokes_index, mask, xy_origin, partial_profiles, progress)) {
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

    if (IsComputedStokes(stokes_index)) { // Need to re-calculate the lattice coordinate region for computed stokes index
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

        auto get_stokes_profiles_data = [&](ProfilesMap& tmp_partial_profiles, int tmp_stokes) {
            StokesRegion stokes_region;
            bool per_z(true); // Get per-z stats data for region for all stats (for cache)
            return (ApplyRegionToFile(region_id, file_id, partial_z_range, tmp_stokes, lc_region, stokes_region) &&
                    _frames.at(file_id)->GetRegionStats(stokes_region, _spectral_stats, per_z, tmp_partial_profiles));
        };

        auto get_profiles_data = [&](ProfilesMap& tmp_partial_profiles, std::string tmp_coordinate) {
            int tmp_stokes;
            return (_frames.at(file_id)->GetStokesTypeIndex(tmp_coordinate, tmp_stokes) &&
                    get_stokes_profiles_data(tmp_partial_profiles, tmp_stokes));
        };

        ProfilesMap partial_profiles;
        if (IsComputedStokes(stokes_index)) { // For computed stokes
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

// ***** Fill stats data *****

bool RegionHandler::FillRegionStatsData(std::function<void(CARTA::RegionStatsData stats_data)> cb, int region_id, int file_id) {
    // Fill stats data for given region and file
    if (!RegionFileIdsValid(region_id, file_id, true)) {
        return false;
    }

    bool message_filled(false);

    auto send_stats_results = [&](int region_id, int file_id, std::vector<CARTA::SetStatsRequirements_StatsConfig> stats_configs) {
        for (auto stats_config : stats_configs) {
            // Get stokes index
            int stokes;
            if (!_frames.at(file_id)->GetStokesTypeIndex(stats_config.coordinate(), stokes)) {
                continue;
            }

            // Set required stats types
            std::vector<CARTA::StatsType> required_stats;
            for (int i = 0; i < stats_config.stats_types_size(); ++i) {
                required_stats.push_back(stats_config.stats_types(i));
            }

            // Send stats results
            CARTA::RegionStatsData stats_message;
            if (GetRegionStatsData(region_id, file_id, stokes, required_stats, stats_message)) {
                cb(stats_message); // send stats data with respect to stokes
                message_filled = true;
            }
        }
    };

    if (region_id > 0) {
        // Fill stats data for specific region with file_id requirement (specific file_id or all files)
        std::unordered_map<ConfigId, RegionStatsConfig, ConfigIdHash> region_configs = _stats_req;
        for (auto& region_config : region_configs) {
            if ((region_config.first.region_id == region_id) && ((region_config.first.file_id == file_id) || (file_id == ALL_FILES))) {
                if (region_config.second.stats_configs.empty()) { // no requirements
                    continue;
                }
                int config_file_id(region_config.first.file_id);
                if (!RegionFileIdsValid(region_id, config_file_id)) { // check specific ids
                    continue;
                }

                // return stats for this requirement
                send_stats_results(region_id, config_file_id, region_config.second.stats_configs);
            }
        }
    } else {
        // (region_id < 0) Fill stats data for all regions with specific file_id requirement
        // Find requirements with file_id
        std::unordered_map<ConfigId, RegionStatsConfig, ConfigIdHash> region_configs = _stats_req;
        for (auto& region_config : region_configs) {
            if (region_config.first.file_id == file_id) {
                if (region_config.second.stats_configs.empty()) { // no requirements
                    continue;
                }
                int config_region_id(region_config.first.region_id);
                if (!RegionFileIdsValid(config_region_id, file_id)) { // check specific ids
                    continue;
                }

                // return stats for this requirement
                send_stats_results(config_region_id, file_id, region_config.second.stats_configs);
            }
        }
    }
    return message_filled;
}

bool RegionHandler::GetRegionStatsData(
    int region_id, int file_id, int stokes, const std::vector<CARTA::StatsType>& required_stats, CARTA::RegionStatsData& stats_message) {
    // Fill stats message for given region, file
    Timer t;

    int z(_frames.at(file_id)->CurrentZ());
    stokes = (stokes == CURRENT_STOKES) ? _frames.at(file_id)->CurrentStokes() : stokes;

    // Start filling message
    stats_message.set_file_id(file_id);
    stats_message.set_region_id(region_id);
    stats_message.set_channel(z);
    stats_message.set_stokes(stokes);

    // Check cache
    CacheId cache_id = CacheId(file_id, region_id, stokes, z);
    if (_stats_cache.count(cache_id)) {
        std::map<CARTA::StatsType, double> stats_results;
        if (_stats_cache[cache_id].GetStats(stats_results)) {
            FillStatistics(stats_message, required_stats, stats_results);
            return true;
        }
    }

    // Get region
    AxisRange z_range(z);
    StokesRegion stokes_region;
    std::shared_ptr<casacore::LCRegion> lc_region;
    if (!ApplyRegionToFile(region_id, file_id, z_range, stokes, lc_region, stokes_region)) {
        // region outside image: NaN results
        std::map<CARTA::StatsType, double> stats_results;
        for (const auto& carta_stat : required_stats) {
            if (carta_stat == CARTA::StatsType::NumPixels) {
                stats_results[carta_stat] = 0.0;
            } else {
                stats_results[carta_stat] = nan("");
            }
        }
        FillStatistics(stats_message, required_stats, stats_results);
        // cache results
        _stats_cache[cache_id] = StatsCache(stats_results);
        return true;
    }

    // calculate stats
    bool per_z(false);
    std::map<CARTA::StatsType, std::vector<double>> stats_map;
    if (_frames.at(file_id)->GetRegionStats(stokes_region, required_stats, per_z, stats_map)) {
        // convert vector to single value in map
        std::map<CARTA::StatsType, double> stats_results;
        for (auto& value : stats_map) {
            stats_results[value.first] = value.second[0];
        }

        // add values to message
        FillStatistics(stats_message, required_stats, stats_results);
        // cache results
        _stats_cache[cache_id] = StatsCache(stats_results);

        spdlog::performance("Fill region stats in {:.3f} ms", t.Elapsed().ms());
        return true;
    }

    return false;
}

bool RegionHandler::FillPointSpatialProfileData(int file_id, int region_id, std::vector<CARTA::SpatialProfileData>& spatial_data_vec) {
    // Cursor/point spatial profiles
    if (!RegionFileIdsValid(region_id, file_id, true)) {
        return false;
    }

    if (!IsPointRegion(region_id)) {
        return false;
    }

    ConfigId config_id(file_id, region_id);
    if (!_spatial_req.count(config_id)) {
        return false;
    }

    // Map a region (region_id) to an image (file_id)
    auto lcregion = ApplyRegionToFile(region_id, file_id);
    if (!lcregion) {
        return false;
    }

    // Get point region
    casacore::IPosition origin = lcregion->boundingBox().start();
    PointXy point(origin(0), origin(1));

    // Get profile for point region
    return _frames.at(file_id)->FillSpatialProfileData(point, _spatial_req.at(config_id), spatial_data_vec);
}

bool RegionHandler::FillLineSpatialProfileData(int file_id, int region_id, std::function<void(CARTA::SpatialProfileData profile_data)> cb) {
    // Line spatial profiles.  Use callback to return each profile individually.
    Timer t;
    if (!RegionFileIdsValid(region_id, file_id, true)) {
        return false;
    }

    if (!IsLineRegion(region_id)) {
        return false;
    }

    ConfigId config_id(file_id, region_id);
    if (!_spatial_req.count(config_id)) {
        return false;
    }

    std::unique_lock<std::mutex> ulock(_spatial_mutex);
    std::vector<CARTA::SetSpatialRequirements_SpatialConfig> line_spatial_configs = _spatial_req.at(config_id);
    ulock.unlock();

    int channel = _frames.at(file_id)->CurrentZ();
    int x(0), y(0), start(0), end(0), mip(0);
    float value(0.0);
    bool profile_ok(false);

    auto region_type = GetRegion(region_id)->GetRegionState().type; // line or polyline
    CARTA::ProfileAxisType axis_type =
        (region_type == CARTA::RegionType::LINE ? CARTA::ProfileAxisType::Offset : CARTA::ProfileAxisType::Distance);

    for (auto& config : line_spatial_configs) {
        std::string coordinate(config.coordinate());
        int width(config.width());

        int stokes_index(0);
        if (!_frames.at(file_id)->GetStokesTypeIndex(coordinate, stokes_index)) {
            continue;
        }

        // Cancel if channel changed or requirement removed
        if (channel != _frames.at(file_id)->CurrentZ()) {
            return false;
        }
        if (!HasSpatialRequirements(region_id, file_id, coordinate, width)) {
            continue;
        }

        profile_ok = GetLineSpatialData(
            file_id, region_id, coordinate, stokes_index, width, [&](std::vector<float>& profile, casacore::Quantity& increment) {
                auto profile_size = profile.size();
                int end = profile_size - 1;
                float crpix = profile_size / 2;
                float cdelt = increment.getValue();
                float crval = (axis_type == CARTA::ProfileAxisType::Offset ? 0.0 : crpix * cdelt);
                std::string unit = increment.getUnit();

                auto profile_message = Message::SpatialProfileData(file_id, region_id, x, y, channel, stokes_index, value, start, end,
                    profile, coordinate, mip, axis_type, crpix, crval, cdelt, unit);
                cb(profile_message);
            });
        spdlog::performance("Fill line spatial profile in {:.3f} ms", t.Elapsed().ms());
    }

    spdlog::performance("Line spatial data in {:.3f} ms", t.Elapsed().ms());
    return profile_ok;
}

bool RegionHandler::GetLineSpatialData(int file_id, int region_id, const std::string& coordinate, int stokes_index, int width,
    const std::function<void(std::vector<float>&, casacore::Quantity&)>& spatial_profile_callback) {
    AxisRange z_range(_frames.at(file_id)->CurrentZ());
    bool per_z(false), cancelled(false);
    GeneratorProgressCallback progress_callback = [](float progress) {}; // no callback for spatial profile
    casacore::Quantity increment;
    casacore::Matrix<float> line_profile;
    std::string message;

    if (GetLineProfiles(file_id, region_id, width, z_range, per_z, stokes_index, coordinate, progress_callback, line_profile, increment,
            cancelled, message)) {
        // Check for cancel
        if (!HasSpatialRequirements(region_id, file_id, coordinate, width)) {
            return false;
        }

        if (!line_profile.empty()) {
            auto profile = line_profile.tovector();
            spatial_profile_callback(profile, increment);
            return true;
        }

        return false;
    } else {
        if (cancelled) {
            spdlog::info("Line region {} spatial profile was cancelled.", region_id);
        } else {
            spdlog::error("Line region {} spatial profile failed: {}", region_id, message);
        }
    }

    return false;
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

std::vector<int> RegionHandler::GetSpatialReqRegionsForFile(int file_id) {
    std::vector<int> results;
    std::unique_lock<std::mutex> ulock(_spatial_mutex);
    for (auto& region : _spatial_req) {
        if (region.first.file_id == file_id) {
            results.push_back(region.first.region_id);
        }
    }
    ulock.unlock();

    return results;
}

std::vector<int> RegionHandler::GetSpatialReqFilesForRegion(int region_id) {
    std::vector<int> results;
    std::unique_lock<std::mutex> ulock(_spatial_mutex);
    for (auto& region : _spatial_req) {
        if (region.first.region_id == region_id) {
            results.push_back(region.first.file_id);
        }
    }
    ulock.unlock();

    return results;
}

bool RegionHandler::GetLineProfiles(int file_id, int region_id, int width, const AxisRange& z_range, bool per_z, int stokes_index,
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
        size_t iprofile;
        // Use completed profiles (not iprofile) for progress.
        // iprofile not in order so progress is uneven.
        size_t completed_profiles(0);

        // Return this to column 0 when uncomment (moved for format check):
        // #pragma omp parallel for private(iprofile) shared(progress, t_start, completed_profiles)
        for (iprofile = 0; iprofile < num_profiles; ++iprofile) {
            if (cancelled) {
                continue;
            }

            // Frame/region closing, or line changed
            if (CancelLineProfiles(region_id, file_id, line_region_state)) {
                cancelled = true;
            }

            // PV generator: check if user canceled
            if (per_z && _stop_pv[file_id]) {
                spdlog::debug("Stopping line profiles: PV generator cancelled");
                cancelled = true;
            }

            // Line spatial profile: check if requirements removed
            if (!per_z && !HasSpatialRequirements(region_id, file_id, coordinate, width)) {
                cancelled = true;
            }

            if (cancelled) {
                profiles.resize();
                continue;
            }

            // Get mean profile for requested file_id and log number of pixels in region
            double num_pixels(0.0);
            casacore::Vector<float> region_profile = GetTemporaryRegionProfile(
                iprofile, file_id, box_regions[iprofile], line_coord_sys, per_z, z_range, stokes_index, num_pixels);
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

            progress = float(++completed_profiles) / float(num_profiles);

            if (per_z) {
                // Update progress if time interval elapsed
                auto t_end = std::chrono::high_resolution_clock::now();
                auto dt = std::chrono::duration<double, std::milli>(t_end - t_start).count();

                if ((dt > LINE_PROFILE_PROGRESS_INTERVAL) || (progress >= 1.0)) {
                    t_start = t_end;
                    progress_callback(progress);
                }
            }
        }
    }

    return (!cancelled) && (progress >= 1.0) && !allEQ(profiles, NAN);
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

casacore::Vector<float> RegionHandler::GetTemporaryRegionProfile(int region_idx, int file_id, RegionState& region_state,
    std::shared_ptr<casacore::CoordinateSystem> reference_csys, bool per_z, const AxisRange& z_range, int stokes_index,
    double& num_pixels) {
    // Create temporary region with RegionState and CoordinateSystem
    // Return stats/spectral profile (depending on per_z) for given file_id image, and number of pixels in the region.

    // Initialize return values
    auto profile_size = per_z ? (z_range.to - z_range.from + 1) : 1;
    casacore::Vector<float> profile(profile_size, NAN);
    num_pixels = 0.0;

    if (!region_state.RegionDefined()) {
        return profile;
    }

    std::lock_guard<std::mutex> guard(_line_profile_mutex);
    // Set temporary region
    int region_id(TEMP_REGION_ID);
    SetRegion(region_id, region_state, reference_csys);
    if (!RegionSet(region_id, true)) {
        return profile;
    }

    if (per_z) {
        // Temp region spectral requirements
        std::vector<CARTA::StatsType> required_stats = {CARTA::StatsType::NumPixels, CARTA::StatsType::Mean};
        ConfigId config_id(file_id, region_id);
        std::string coordinate("z"); // current stokes
        SpectralConfig spectral_config(coordinate, required_stats);
        RegionSpectralConfig region_config;
        region_config.configs.push_back(spectral_config);

        // Check cancel: currently temp per-z profile is only for PV image generator
        if (_stop_pv[file_id]) {
            RemoveRegion(region_id);
            return profile;
        }

        // Set region spectral requirements
        std::unique_lock<std::mutex> ulock(_spectral_mutex);
        _spectral_req[config_id] = region_config;
        ulock.unlock();

        // Do not report errors for line regions outside image
        bool report_error(false);

        // Get region spectral profiles converted to file_id image if necessary
        GetRegionSpectralData(region_id, file_id, z_range, coordinate, stokes_index, required_stats, report_error,
            [&](std::map<CARTA::StatsType, std::vector<double>> results, float progress) {
                // Callback only sets return values when progress is complete.
                if (progress == 1.0) {
                    // Get mean spectral profile and max NumPixels for small region.
                    auto npix_per_chan = results[CARTA::StatsType::NumPixels];
                    num_pixels = *max_element(npix_per_chan.begin(), npix_per_chan.end());
                    profile = casacore::Vector<float>(results[CARTA::StatsType::Mean]); // TODO: use double for profile
                }
            });
    } else {
        // Use BasicStats to get num_pixels and mean for current channel and stokes
        // Get region as LCRegion
        StokesRegion stokes_region;
        std::shared_ptr<casacore::LCRegion> lc_region;
        std::shared_lock frame_lock(_frames.at(file_id)->GetActiveTaskMutex());
        if (ApplyRegionToFile(region_id, file_id, z_range, stokes_index, lc_region, stokes_region)) {
            // Get region data by applying LCRegion to image
            std::vector<float> region_data;
            if (_frames.at(file_id)->GetRegionData(stokes_region, region_data, false)) {
                // Very small region, just calc needed stats here
                num_pixels = 0;
                double sum(0.0);
                for (size_t i = 0; i < region_data.size(); ++i) {
                    float val(region_data[i]);
                    if (std::isfinite(val)) {
                        num_pixels++;
                        sum += (double)val;
                    }
                }
                if (num_pixels > 0) {
                    profile[0] = sum / num_pixels;
                }
            }
        }
        frame_lock.unlock();
    }

    // Remove temporary region
    RemoveRegion(region_id);

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
    auto calc_fpi = [&](double i, double pi) { return (IsValid(i, pi) ? 100.0 * (pi / i) : std::numeric_limits<double>::quiet_NaN()); };

    CombineStokes(profiles_pflinear, profiles_q, profiles_u, calc_pi);
    CombineStokes(profiles_pflinear, profiles_i, calc_fpi);
}

void RegionHandler::GetStokesPangle(const ProfilesMap& profiles_q, const ProfilesMap& profiles_u, ProfilesMap& profiles_pangle) {
    auto calc_pa = [&](double q, double u) { return (180.0 / casacore::C::pi) * atan2(u, q) / 2; };

    CombineStokes(profiles_pangle, profiles_q, profiles_u, calc_pa);
}

void RegionHandler::CombineStokes(ProfilesMap& profiles_out, const ProfilesMap& profiles_q, const ProfilesMap& profiles_u,
    const std::function<double(double, double)>& func) {
    auto func_if_valid = [&](double a, double b) { return (IsValid(a, b) ? func(a, b) : std::numeric_limits<double>::quiet_NaN()); };

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
    auto func_if_valid = [&](double a, double b) { return (IsValid(a, b) ? func(a, b) : std::numeric_limits<double>::quiet_NaN()); };

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
    ProfilesMap& profiles, int stokes, const std::function<bool(ProfilesMap&, std::string)>& get_profiles_data) {
    ProfilesMap profile_i, profile_q, profile_u, profile_v;
    if (stokes == COMPUTE_STOKES_PTOTAL) {
        if (!get_profiles_data(profile_q, "Qz") || !get_profiles_data(profile_u, "Uz") || !get_profiles_data(profile_v, "Vz")) {
            return false;
        }
        GetStokesPtotal(profile_q, profile_u, profile_v, profiles);
    } else if (stokes == COMPUTE_STOKES_PFTOTAL) {
        if (!get_profiles_data(profile_i, "Iz") || !get_profiles_data(profile_q, "Qz") || !get_profiles_data(profile_u, "Uz") ||
            !get_profiles_data(profile_v, "Vz")) {
            return false;
        }
        GetStokesPftotal(profile_i, profile_q, profile_u, profile_v, profiles);
    } else if (stokes == COMPUTE_STOKES_PLINEAR) {
        if (!get_profiles_data(profile_q, "Qz") || !get_profiles_data(profile_u, "Uz")) {
            return false;
        }
        GetStokesPlinear(profile_q, profile_u, profiles);
    } else if (stokes == COMPUTE_STOKES_PFLINEAR) {
        if (!get_profiles_data(profile_i, "Iz") || !get_profiles_data(profile_q, "Qz") || !get_profiles_data(profile_u, "Uz")) {
            return false;
        }
        GetStokesPflinear(profile_i, profile_q, profile_u, profiles);
    } else if (stokes == COMPUTE_STOKES_PANGLE) {
        if (!get_profiles_data(profile_q, "Qz") || !get_profiles_data(profile_u, "Uz")) {
            return false;
        }
        GetStokesPangle(profile_q, profile_u, profiles);
    }
    return true;
}

} // namespace carta
