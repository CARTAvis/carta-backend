/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// RegionDataHandler.cc: handle requirements and data streams for regions

#include "RegionHandler.h"

#include <chrono>
#include <cmath>

#include <casacore/lattices/LRegions/LCBox.h>
#include <casacore/lattices/LRegions/LCExtension.h>
#include <casacore/lattices/LRegions/LCIntersection.h>

#include "../ImageStats/StatsCalculator.h"
#include "../Logger/Logger.h"
#include "Util/File.h"
#include "Util/Image.h"

#include "CrtfImportExport.h"
#include "Ds9ImportExport.h"
#include "Util/Message.h"

#define LINE_PROFILE_PROGRESS_INTERVAL 500

namespace carta {

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

bool RegionHandler::SetRegion(int& region_id, RegionState& region_state, std::shared_ptr<casacore::CoordinateSystem> csys) {
    // Set region params for region id; if id < 0, create new id
    // CoordinateSystem will be owned by Region
    bool valid_region(false);
    if (_regions.count(region_id)) {
        _regions.at(region_id)->UpdateRegion(region_state);
        valid_region = _regions.at(region_id)->IsValid();
        if (_regions.at(region_id)->RegionChanged()) {
            UpdateNewSpectralRequirements(region_id); // set all req "new"
            ClearRegionCache(region_id);
        }
    } else {
        if ((region_id < 0) && (region_id != TEMP_REGION_ID)) {
            region_id = GetNextRegionId();
        }
        auto region = std::shared_ptr<Region>(new Region(region_state, csys));
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
            region.second->WaitForTaskCancellation();
        }
        _regions.clear();
    } else if (_regions.count(region_id)) {
        _regions.at(region_id)->WaitForTaskCancellation();
        _regions.erase(region_id);
    }
    RemoveRegionRequirementsCache(region_id);
}

std::shared_ptr<Region> RegionHandler::GetRegion(int region_id) {
    if (RegionSet(region_id)) {
        return _regions.at(region_id);
    } else {
        return std::shared_ptr<Region>();
    }
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
    auto csys = frame->CoordinateSystem();
    const casacore::IPosition shape = frame->ImageShape();
    std::unique_ptr<RegionImportExport> importer;
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
        auto region_csys = frame->CoordinateSystem();
        auto region_state = imported_region.state;
        auto region = std::shared_ptr<Region>(new Region(region_state, region_csys));

        if (region && region->IsValid()) {
            _regions[region_id] = std::move(region);

            // Set CARTA::RegionInfo
            CARTA::RegionInfo carta_region_info;
            carta_region_info.set_region_type(region_state.type);
            *carta_region_info.mutable_control_points() = {region_state.control_points.begin(), region_state.control_points.end()};
            carta_region_info.set_rotation(region_state.rotation);
            // Set CARTA::RegionStyle
            auto region_style = imported_region.style;
            CARTA::RegionStyle carta_region_style;
            carta_region_style.set_name(region_style.name);
            carta_region_style.set_color(region_style.color);
            carta_region_style.set_line_width(region_style.line_width);
            *carta_region_style.mutable_dash_list() = {region_style.dash_list.begin(), region_style.dash_list.end()};

            // Add info and style to import_ack; increment region id for next region
            (*region_info_map)[region_id] = carta_region_info;
            (*region_style_map)[region_id++] = carta_region_style;
        }
    }
}

void RegionHandler::ExportRegion(int file_id, std::shared_ptr<Frame> frame, CARTA::FileType region_file_type,
    CARTA::CoordinateType coord_type, std::map<int, CARTA::RegionStyle>& region_styles, std::string& filename,
    CARTA::ExportRegionAck& export_ack) {
    // Export regions to given filename, or return export file contents in ack
    // Check if any regions to export
    if (region_styles.empty()) {
        export_ack.set_success(false);
        export_ack.set_message("Export failed: no regions requested.");
        export_ack.add_contents();
        return;
    }

    // Check ability to create export file if filename given
    if (!filename.empty()) {
        casacore::File export_file(filename);
        if (!export_file.canCreate()) {
            export_ack.set_success(false);
            export_ack.set_message("Export region failed: cannot create file.");
            export_ack.add_contents();
            return;
        }
    }

    bool pixel_coord(coord_type == CARTA::CoordinateType::PIXEL);
    auto output_csys = frame->CoordinateSystem();

    if (!pixel_coord && !output_csys->hasDirectionCoordinate()) {
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
            exporter = std::unique_ptr<RegionImportExport>(new Ds9ImportExport(output_csys, output_shape, pixel_coord));
            break;
        default:
            break;
    }

    // Add regions to export from map<region_id, RegionStyle>
    std::string error; // append region errors here
    for (auto& region : region_styles) {
        int region_id = region.first;

        if (_regions.count(region_id)) {
            bool region_added(false);

            // Get state from Region, style from input map
            RegionState region_state = _regions[region_id]->GetRegionState();

            CARTA::RegionStyle carta_region_style = region.second;
            std::vector<int> dash_list = {carta_region_style.dash_list().begin(), carta_region_style.dash_list().end()};
            RegionStyle region_style(carta_region_style.name(), carta_region_style.color(), carta_region_style.line_width(), dash_list);

            if ((region_state.reference_file_id == file_id) && pixel_coord) {
                // Use RegionState control points with reference file id for pixel export
                region_added = exporter->AddExportRegion(region_state, region_style);
            } else {
                try {
                    // Use Record containing pixel coords of region converted to output image
                    casacore::TableRecord region_record = _regions.at(region_id)->GetImageRegionRecord(file_id, output_csys, output_shape);
                    if (!region_record.empty()) {
                        region_added = exporter->AddExportRegion(region_state, region_style, region_record, pixel_coord);
                    }
                } catch (const casacore::AipsError& err) {
                    spdlog::error("Export region record failed: {}", err.getMesg());
                }
            }

            if (!region_added) {
                std::string region_error = fmt::format("Export region {} in image {} failed.\n", region_id, file_id);
                error.append(region_error);
            }
        } else {
            std::string region_error = fmt::format("Region {} not found for export.\n", region_id);
            error.append(region_error);
        }
    }

    bool success(false);
    if (filename.empty()) {
        // Return contents
        std::vector<std::string> line_contents;
        if (exporter->ExportRegions(line_contents, error)) {
            success = true;
            *export_ack.mutable_contents() = {line_contents.begin(), line_contents.end()};
        }
    } else {
        // Write to file
        if (exporter->ExportRegions(filename, error)) {
            success = true;
        }
    }

    // Export failed
    export_ack.set_success(success);
    export_ack.set_message(error);
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
        _stop_pv[file_id] = true;
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
            HistogramConfig hist_config(config.coordinate(), config.channel(), config.num_bins());
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

    if (!_regions.count(region_id)) {
        spdlog::error("Spectral requirements failed: no region with id {}", region_id);
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
    int region_id, int file_id, std::string& coordinate, std::vector<CARTA::StatsType>& required_stats) {
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
    // Set stats data requirements for given region and file
    if (stats_configs.empty() && !RegionSet(region_id)) {
        // frontend clears requirements after region removed, prevent error in log
        return true;
    }

    if (_regions.count(region_id)) {
        // Save frame pointer
        _frames[file_id] = frame;

        // Set requirements
        ConfigId config_id(file_id, region_id);
        _stats_req[config_id].stats_configs = stats_configs;
        return true;
    }
    return false;
}

void RegionHandler::RemoveRegionRequirementsCache(int region_id) {
    // Clear requirements and cache for a specific region or for all regions when closed
    if (region_id == ALL_REGIONS) {
        _histogram_req.clear();
        _stats_req.clear();
        std::unique_lock<std::mutex> ulock(_spectral_mutex);
        _spectral_req.clear();
        ulock.unlock();

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

        std::unique_lock<std::mutex> ulock(_spectral_mutex);
        for (auto it = _spectral_req.begin(); it != _spectral_req.end();) {
            if ((*it).first.region_id == region_id) {
                it = _spectral_req.erase(it);
            } else {
                ++it;
            }
        }
        ulock.unlock();

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
        _stats_req.clear();
        std::unique_lock<std::mutex> ulock(_spectral_mutex);
        _spectral_req.clear();
        ulock.unlock();

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

        std::unique_lock<std::mutex> ulock(_spectral_mutex);
        for (auto it = _spectral_req.begin(); it != _spectral_req.end();) {
            if ((*it).first.file_id == file_id) {
                it = _spectral_req.erase(it);
            } else {
                ++it;
            }
        }
        ulock.unlock();

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

std::shared_ptr<casacore::LCRegion> RegionHandler::ApplyRegionToFile(
    int region_id, int file_id, const StokesSource& stokes_source, bool report_error) {
    // Returns 2D region with no extension; nullptr if outside image or not closed region
    // Go through Frame for image mutex
    if (!RegionFileIdsValid(region_id, file_id)) {
        return nullptr;
    }

    if (_regions.at(region_id)->IsAnnotation()) { // true if line or polyline
        return nullptr;
    }

    return _frames.at(file_id)->GetImageRegion(file_id, _regions.at(region_id), stokes_source, report_error);
}

bool RegionHandler::ApplyRegionToFile(int region_id, int file_id, const AxisRange& z_range, int stokes, StokesRegion& stokes_region,
    std::shared_ptr<casacore::LCRegion> region_2D) {
    // Returns 3D image region for region applied to image and extended by z-range and stokes index
    if (!RegionFileIdsValid(region_id, file_id)) {
        return false;
    }

    try {
        StokesSource stokes_source(stokes, z_range);
        stokes_region.stokes_source = stokes_source;
        auto applied_region = region_2D;
        if (!applied_region) {
            applied_region = ApplyRegionToFile(region_id, file_id, stokes_source);
        }
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

bool RegionHandler::CalculateMoments(int file_id, int region_id, const std::shared_ptr<Frame>& frame,
    GeneratorProgressCallback progress_callback, const CARTA::MomentRequest& moment_request, CARTA::MomentResponse& moment_response,
    std::vector<GeneratedImage>& collapse_results) {
    StokesRegion stokes_region;
    std::shared_ptr<casacore::LCRegion> lc_region;
    int z_min(moment_request.spectral_range().min());
    int z_max(moment_request.spectral_range().max());

    // Do calculations
    if (ApplyRegionToFile(region_id, file_id, AxisRange(z_min, z_max), frame->CurrentStokes(), stokes_region, lc_region)) {
        frame->CalculateMoments(file_id, progress_callback, stokes_region, moment_request, moment_response, collapse_results);
    }
    return !collapse_results.empty();
}

bool RegionHandler::CalculatePvImage(int file_id, int region_id, int width, std::shared_ptr<Frame>& frame,
    GeneratorProgressCallback progress_callback, CARTA::PvResponse& pv_response, GeneratedImage& pv_image) {
    // Generate PV image by approximating line as box regions and getting spectral profile for each.
    // Returns whether PV image was generated.
    pv_response.set_success(false);
    pv_response.set_cancel(false);

    // Checks for valid request:
    // 1. Region is set
    if (!RegionSet(region_id)) {
        pv_response.set_message("PV image generator requested for invalid region.");
        return false;
    }

    // 2. Region is line type but not polyline
    auto region = _regions.at(region_id);
    if (!region->IsAnnotation()) {
        pv_response.set_message("Region type not supported for PV image generator.");
        return false;
    }
    auto region_state = region->GetRegionState();
    if (region_state.type == CARTA::RegionType::POLYLINE) {
        pv_response.set_message("Region type POLYLINE not supported yet for PV image generator.");
        return false;
    }

    // 3. Image has spectral axis
    if (frame->SpectralAxis() < 0) {
        pv_response.set_message("No spectral axis for generating PV image.");
        return false;
    }

    // Reset stop flag
    _stop_pv[file_id] = false;

    bool add_frame = !FrameSet(file_id);
    if (add_frame) {
        _frames[file_id] = frame;
    }

    bool pv_success(false), per_z(true), cancelled(false);
    double increment(0.0);           // Increment between box centers returned in arcsec
    casacore::Matrix<float> pv_data; // Spectral profiles for each box region: shape=[num_regions, num_channels]
    std::string message;

    if (GetLineProfiles(file_id, region_id, width, per_z, progress_callback, increment, pv_data, cancelled, message)) {
        if (!_stop_pv[file_id]) {
            // Use PV generator to create PV image
            auto input_filename = frame->GetFileName();
            PvGenerator pv_generator(file_id, input_filename);

            auto input_image = frame->GetImage();
            pv_success = pv_generator.GetPvImage(input_image, pv_data, increment, frame->CurrentStokes(), pv_image, message);

            frame->CloseCachedImage(input_filename);
        }
    }

    // Clean up
    cancelled = _stop_pv[file_id]; // Final check
    if (add_frame) {
        RemoveFrame(file_id); // Sets stop flag in case image closed during pv generation
    }
    _stop_pv.erase(file_id);

    if (cancelled) {
        pv_success = false;
        message = "PV image generator cancelled.";
        spdlog::debug(message);
    }

    // Complete message
    pv_response.set_success(pv_success);
    pv_response.set_message(message);
    pv_response.set_cancel(cancelled);

    return pv_success;
}

void RegionHandler::StopPvCalc(int file_id) {
    _stop_pv[file_id] = true;

    // Stop spectral profile in progress - clear requirements. Returns if region ID not set.
    if (FrameSet(file_id)) {
        std::vector<CARTA::SetSpectralRequirements_SpectralConfig> no_profiles;
        SetSpectralRequirements(TEMP_REGION_ID, file_id, _frames.at(file_id), no_profiles);
    }
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
    int region_id, int file_id, const std::vector<HistogramConfig>& configs, std::vector<CARTA::RegionHistogramData>& histogram_messages) {
    // Fill stats message for given region, file
    auto t_start_region_histogram = std::chrono::high_resolution_clock::now();

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
        CARTA::RegionHistogramData histogram_message;
        histogram_message.set_file_id(file_id);
        histogram_message.set_region_id(region_id);
        histogram_message.set_progress(1.0); // only cube histograms have partial results
        histogram_message.set_channel(z);
        histogram_message.set_stokes(stokes);

        // Get image region
        if (!ApplyRegionToFile(region_id, file_id, z_range, stokes, stokes_region, lc_region)) {
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
                Histogram hist;
                if (_histogram_cache[cache_id].GetHistogram(num_bins, hist)) {
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

        // Calculate and cache histogram for number of bins
        Histogram histo = CalcHistogram(num_bins, stats, data[stokes].data(), data[stokes].size());
        _histogram_cache[cache_id].SetHistogram(num_bins, histo);

        // Complete Histogram submessage
        auto* histogram = histogram_message.mutable_histograms();
        FillHistogram(histogram, stats, histo);

        // Fill in the final result
        histogram_messages.emplace_back(histogram_message);
    }

    auto t_end_region_histogram = std::chrono::high_resolution_clock::now();
    auto dt_region_histogram =
        std::chrono::duration_cast<std::chrono::microseconds>(t_end_region_histogram - t_start_region_histogram).count();
    spdlog::performance(
        "Fill region histogram in {:.3f} ms at {:.3f} MPix/s", dt_region_histogram * 1e-3, (float)stats.num_pixels / dt_region_histogram);

    return true;
}

// ***** Fill spectral profile *****

bool RegionHandler::FillSpectralProfileData(
    std::function<void(CARTA::SpectralProfileData profile_data)> cb, int region_id, int file_id, bool stokes_changed) {
    // Fill spectral profiles for given region and file ids.  This could be:
    // 1. a specific region and a specific file
    // 2. a specific region and ALL_FILES
    // 3. a specific file and ALL_REGIONS

    if (!RegionFileIdsValid(region_id, file_id)) {
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
                profile_ok = GetRegionSpectralData(config_region_id, config_file_id, coordinate, stokes_index, required_stats, report_error,
                    [&](std::map<CARTA::StatsType, std::vector<double>> results, float progress) {
                        CARTA::SpectralProfileData profile_message = Message::SpectralProfileData(
                            config_file_id, config_region_id, stokes_index, progress, coordinate, required_stats, results);
                        cb(profile_message); // send (partial profile) data
                    });
            }
        }
    }

    return profile_ok;
}

bool RegionHandler::GetRegionSpectralData(int region_id, int file_id, std::string& coordinate, int stokes_index,
    std::vector<CARTA::StatsType>& required_stats, bool report_error,
    const std::function<void(std::map<CARTA::StatsType, std::vector<double>>, float)>& partial_results_callback) {
    // Fill spectral profile message for given region, file, and requirement
    if (!RegionFileIdsValid(region_id, file_id)) {
        return false;
    }

    // Check cancel
    if (!HasSpectralRequirements(region_id, file_id, coordinate, required_stats)) {
        return false;
    }

    bool use_current_stokes(coordinate == "z");

    auto t_start_spectral_profile = std::chrono::high_resolution_clock::now();

    std::shared_lock frame_lock(_frames.at(file_id)->GetActiveTaskMutex());
    std::shared_lock region_lock(_regions.at(region_id)->GetActiveTaskMutex());

    // Initialize results map for requested stats to NaN, progress to zero
    size_t profile_size = _frames.at(file_id)->Depth();
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

    // Get 2D region to check if inside image
    auto lc_region = ApplyRegionToFile(region_id, file_id, StokesSource(), report_error); // Apply region to the original image coordinate
    if (!lc_region) {
        progress = 1.0;
        partial_results_callback(results, progress); // region outside image, send NaNs
        return true;
    }

    // Get initial region info to cancel profile if it changes
    RegionState initial_region_state = _regions.at(region_id)->GetRegionState();

    // Use loader swizzled data for efficiency
    if (_frames.at(file_id)->UseLoaderSpectralData(lc_region->shape())) {
        // Use cursor spectral profile for point region
        if (initial_region_state.type == CARTA::RegionType::POINT) {
            casacore::IPosition origin = lc_region->boundingBox().start();
            CARTA::Point point;
            point.set_x(origin(0));
            point.set_y(origin(1));

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
        casacore::ArrayLattice<casacore::Bool> mask = _regions[region_id]->GetImageRegionMask(file_id);
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
                if (_regions.at(region_id)->GetRegionState() != initial_region_state) {
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
                    return (_frames.at(file_id)->GetStokesTypeIndex(tmp_coordinate, tmp_stokes) &&
                            _frames.at(file_id)->GetLoaderSpectralData(region_id, tmp_stokes, mask, xy_origin, tmp_results, progress));
                };

                ProfilesMap partial_profiles;
                if (IsComputedStokes(stokes_index)) { // For computed stokes
                    if (!GetComputedStokesProfiles(partial_profiles, stokes_index, get_profiles_data)) {
                        return false;
                    }
                } else { // For regular stokes I, Q, U, or V
                    if (!_frames.at(file_id)->GetLoaderSpectralData(region_id, stokes_index, mask, xy_origin, partial_profiles, progress)) {
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

            auto t_end_spectral_profile = std::chrono::high_resolution_clock::now();
            auto dt_spectral_profile =
                std::chrono::duration_cast<std::chrono::microseconds>(t_end_spectral_profile - t_start_spectral_profile).count();
            spdlog::performance("Fill spectral profile in {:.3f} ms", dt_spectral_profile * 1e-3);
            return true;
        }
    } // end loader swizzled data

    // Initialize cache results for *all* spectral stats
    std::map<CARTA::StatsType, std::vector<double>> cache_results;
    for (const auto& stat : _spectral_stats) {
        cache_results[stat] = init_spectral;
    }

    // Calculate and cache profiles
    size_t start_z(0), count(0), end_z(0);
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

        end_z = (start_z + delta_z > profile_size ? profile_size - 1 : start_z + delta_z - 1);
        count = end_z - start_z + 1;

        // Get 3D region for z range and stokes_index
        AxisRange z_range(start_z, end_z);

        auto get_stokes_profiles_data = [&](ProfilesMap& tmp_partial_profiles, int tmp_stokes) {
            StokesRegion stokes_region;
            bool per_z(true); // Get per-z stats data for region for all stats (for cache)
            return (ApplyRegionToFile(region_id, file_id, z_range, tmp_stokes, stokes_region, lc_region) &&
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
                memcpy(&results[stats_type][start_z], &stats_data[0], stats_data.size() * sizeof(double));
            }
            memcpy(&cache_results[stats_type][start_z], &stats_data[0], stats_data.size() * sizeof(double));
        }

        start_z += count;
        progress = (float)start_z / profile_size;

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
        if (_regions.at(region_id)->GetRegionState() != initial_region_state) {
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

    auto t_end_spectral_profile = std::chrono::high_resolution_clock::now();
    auto dt_spectral_profile =
        std::chrono::duration_cast<std::chrono::microseconds>(t_end_spectral_profile - t_start_spectral_profile).count();
    spdlog::performance("Fill spectral profile in {:.3f} ms", dt_spectral_profile * 1e-3);

    return true;
}

// ***** Fill stats data *****

bool RegionHandler::FillRegionStatsData(std::function<void(CARTA::RegionStatsData stats_data)> cb, int region_id, int file_id) {
    // Fill stats data for given region and file
    if (!RegionFileIdsValid(region_id, file_id)) {
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
    auto t_start_region_stats = std::chrono::high_resolution_clock::now();

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
    if (!ApplyRegionToFile(region_id, file_id, z_range, stokes, stokes_region, lc_region)) {
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

        auto t_end_region_stats = std::chrono::high_resolution_clock::now();
        auto dt_region_stats = std::chrono::duration_cast<std::chrono::microseconds>(t_end_region_stats - t_start_region_stats).count();
        spdlog::performance("Fill region stats in {:.3f} ms", dt_region_stats * 1e-3);

        return true;
    }

    return false;
}

bool RegionHandler::SetSpatialRequirements(int region_id, int file_id, std::shared_ptr<Frame> frame,
    const std::vector<CARTA::SetSpatialRequirements_SpatialConfig>& spatial_profiles) {
    // Clear all requirements for this file/region
    ConfigId config_id(file_id, region_id);
    if (_spatial_req.count(config_id)) {
        std::unique_lock<std::mutex> ulock(_spatial_mutex);
        _spatial_req[config_id].clear();
        ulock.unlock();
    }

    // Set spatial profile requirements for given region and file
    if (spatial_profiles.empty() && !RegionSet(region_id)) {
        // Frontend clears requirements after region removed, prevent error in log by returning true.
        return true;
    }

    if (!_regions.count(region_id)) {
        spdlog::error("Spatial requirements failed: no region with id {}", region_id);
        return false;
    }

    // Save frame pointer
    _frames[file_id] = frame;

    // Set new requirements for this file/region
    std::unique_lock<std::mutex> ulock(_spatial_mutex);
    _spatial_req[config_id] = spatial_profiles;
    ulock.unlock();

    return true;
}

bool RegionHandler::FillSpatialProfileData(int file_id, int region_id, std::vector<CARTA::SpatialProfileData>& spatial_data_vec) {
    ConfigId config_id(file_id, region_id);
    if (!_regions.count(region_id) || !_frames.count(file_id) || !_spatial_req.count(config_id)) {
        return false;
    }

    // Map a point region (region_id) to an image (file_id)
    auto lc_region = ApplyRegionToFile(region_id, file_id);
    if (!lc_region) {
        return false;
    }

    casacore::IPosition origin = lc_region->boundingBox().start();
    PointXy point(origin(0), origin(1));

    return _frames.at(file_id)->FillSpatialProfileData(point, _spatial_req.at(config_id), spatial_data_vec);
}

bool RegionHandler::IsPointRegion(int region_id) {
    if (_regions.count(region_id)) {
        if (_regions[region_id]->GetRegionState().type == CARTA::RegionType::POINT) {
            return true;
        }
    }
    return false;
}

std::vector<int> RegionHandler::GetPointRegionIds(int file_id) {
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

std::vector<int> RegionHandler::GetProjectedFileIds(int region_id) {
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

bool RegionHandler::GetLineProfiles(int file_id, int region_id, int width, bool per_z, std::function<void(float)>& progress_callback,
    double& increment, casacore::Matrix<float>& profiles, bool& cancelled, std::string& message) {
    // Generate box regions to approximate a line with a width (pixels), and get mean of each box (per z else current z).
    // Input parameters: file_id, region_id, width, per_z (all channels or current channel).
    // Calls progress_callback after each profile.
    // Return parameters: increment (angular spacing of boxes, in arcsec), per-region profiles, cancelled, message.
    // Returns whether profiles completed.
    bool profiles_complete(false);

    // Line box regions are set with reference image coordinate system then converted to file_id image if necessary
    auto reference_csys = _regions.at(region_id)->CoordinateSystem();
    if (!reference_csys->hasDirectionCoordinate()) {
        message = "Cannot approximate line with no direction coordinate.";
        return profiles_complete;
    }

    auto region_state = _regions.at(region_id)->GetRegionState();
    if (region_state.rotation == 0.0) {
        SetLineRotation(region_state);
    }

    if (GetFixedPixelRegionProfiles(
            file_id, width, per_z, region_state, reference_csys, progress_callback, profiles, increment, cancelled)) {
        spdlog::debug("Using fixed pixel increment for line box regions.");
        profiles_complete = true;
    } else if (!cancelled && GetFixedAngularRegionProfiles(file_id, width, per_z, region_state, reference_csys, progress_callback, profiles,
                                 increment, cancelled, message)) {
        spdlog::debug("Using fixed angular increment for line box regions.");
        profiles_complete = true;
    }

    return profiles_complete;
}

void RegionHandler::SetLineRotation(RegionState& region_state) {
    // Not set on line region import
    auto endpoints = region_state.control_points;
    auto x_angle_deg = atan(double(endpoints[1].y() - endpoints[0].y()) / double(endpoints[1].x() - endpoints[0].x())) * 180.0 / M_PI;

    // Rotation measured from north
    auto rotation = x_angle_deg - 90.0;
    if (rotation < 0.0) {
        rotation += 360.0;
    }

    region_state.rotation = rotation;
}

bool RegionHandler::GetFixedPixelRegionProfiles(int file_id, int width, bool per_z, RegionState& region_state,
    std::shared_ptr<casacore::CoordinateSystem> reference_csys, std::function<void(float)>& progress_callback,
    casacore::Matrix<float>& profiles, double& increment, bool& cancelled) {
    // Calculate mean spectral profiles for box regions along line with fixed pixel spacing, with progress updates after each profile.
    // Return parameters include the profiles, the increment between the box centers in arcsec, and whether profiles were cancelled.
    // Returns false if profiles cancelled or linear pixel centers are tabular in world coordinates.
    auto endpoints = region_state.control_points;
    auto dx_pixels = endpoints[1].x() - endpoints[0].x();
    auto dy_pixels = endpoints[1].y() - endpoints[0].y();
    size_t num_regions = sqrt((dx_pixels * dx_pixels) + (dy_pixels * dy_pixels));

    // Offset range [-offset, offset] from center, in pixels
    int num_offsets = lround(float(num_regions - 1) / 2.0);
    auto center_idx = num_offsets;
    std::vector<CARTA::Point> box_centers((num_offsets * 2) + 1);

    // Center point of line
    auto center_x = (endpoints[0].x() + endpoints[1].x()) / 2;
    auto center_y = (endpoints[0].y() + endpoints[1].y()) / 2;

    // Set center pixel at center index
    CARTA::Point point;
    point.set_x(center_x);
    point.set_y(center_y);
    box_centers[center_idx] = point;

    // Apply rotation to get next pixel
    auto rotation = region_state.rotation;
    float cos_x = cos((rotation + 90.0) * M_PI / 180.0f);
    float sin_x = sin((rotation + 90.0) * M_PI / 180.0f);

    // Set pixel direction for horizontal line
    int pixel_dir(1.0);
    if (dy_pixels == 0.0) {
        pixel_dir = (dx_pixels < 0 ? -1.0 : 1.0);
    }

    // Set pixels in pos and neg direction from center out
    for (int ipixel = 1; ipixel <= num_offsets; ++ipixel) {
        // Positive offset
        auto idx = center_idx + ipixel;
        point.set_x(center_x - (pixel_dir * ipixel * cos_x));
        point.set_y(center_y - (pixel_dir * ipixel * sin_x));
        box_centers[idx] = point;

        // Negative offset
        idx = center_idx - ipixel;
        point.set_x(center_x + (pixel_dir * ipixel * cos_x));
        point.set_y(center_y + (pixel_dir * ipixel * sin_x));
        box_centers[idx] = point;
    }

    if (per_z && _stop_pv[file_id]) {
        cancelled = true;
        return false;
    }

    float progress(0.0);
    if (CheckLinearOffsets(box_centers, reference_csys, increment)) {
        size_t num_regions(box_centers.size());
        float height = (fmod(rotation, 90.0) == 0.0 ? 1.0 : 3.0);

        // Send progress updates at time interval
        auto t_start = std::chrono::high_resolution_clock::now();

        // Set box regions from centers, width x 1
        for (size_t i = 0; i < num_regions; ++i) {
            if (per_z && _stop_pv[file_id]) {
                cancelled = true;
                return false;
            }

            // Set temporary region for reference image
            // Rectangle control points: center, width (user-set width), height (1 pixel)
            std::vector<CARTA::Point> control_points;
            control_points.push_back(box_centers[i]);
            CARTA::Point point;
            point.set_x(width);
            point.set_y(height);
            control_points.push_back(point);
            RegionState temp_region_state(region_state.reference_file_id, CARTA::RegionType::RECTANGLE, control_points, rotation);

            // Get mean spectral profile for requested file_id and log number of pixels in region
            double num_pixels(0.0);
            casacore::Vector<float> region_profile =
                GetTemporaryRegionProfile(file_id, temp_region_state, reference_csys, per_z, num_pixels);

            if (!num_pixels && per_z && _stop_pv[file_id]) {
                cancelled = true;
                return false;
            }

            spdlog::debug("Line box region {} max num pixels={}", i, num_pixels);

            if (profiles.empty()) {
                // initialize matrix
                profiles.resize(casacore::IPosition(2, num_regions, region_profile.size()));
                profiles = NAN;
            }

            profiles.row(i) = region_profile;

            // Update progress if time interval elapsed
            auto t_end = std::chrono::high_resolution_clock::now();
            auto dt = std::chrono::duration<double, std::milli>(t_end - t_start).count();
            progress = float(i + 1) / float(num_regions);

            if ((dt > LINE_PROFILE_PROGRESS_INTERVAL) || (progress >= 1.0)) {
                t_start = t_end;
                progress_callback(progress);
            }
        }
    }

    if (per_z) {
        cancelled = _stop_pv[file_id];
    }

    return (progress == 1.0) && !allEQ(profiles, NAN);
}

bool RegionHandler::CheckLinearOffsets(
    const std::vector<CARTA::Point>& box_centers, std::shared_ptr<casacore::CoordinateSystem> csys, double& increment) {
    // Check whether separation between box centers is linear.
    auto direction_coord = csys->directionCoordinate();

    // Convert all center points to world, check angular separation between centers
    size_t num_centers(box_centers.size()), num_separation(0);
    double min_separation, max_separation;
    double total_separation(0.0);
    double tolerance = GetSeparationTolerance(csys);
    casacore::MVDirection mvdir1, mvdir2;
    casacore::Vector<casacore::Double> center_point(2);

    for (size_t i = 0; i < num_centers - 1; ++i) {
        bool check_separation(true);

        // Get this center and next center as MVDirections
        // Center i
        center_point[0] = box_centers[i].x();
        center_point[1] = box_centers[i].y();
        try {
            mvdir1 = direction_coord.toWorld(center_point);
        } catch (casacore::AipsError& err) { // wcslib conversion error
            check_separation = false;
        }

        // Center i + 1
        center_point[0] = box_centers[i + 1].x();
        center_point[1] = box_centers[i + 1].y();
        try {
            mvdir2 = direction_coord.toWorld(center_point);
        } catch (casacore::AipsError& err) { // wcslib conversion error
            check_separation = false;
        }

        // Check separation
        if (check_separation) {
            double center_separation = mvdir1.separation(mvdir2, "arcsec").getValue();

            if (i == 0) {
                min_separation = max_separation = center_separation;
            } else {
                min_separation = (center_separation < min_separation) ? center_separation : min_separation;
                max_separation = (center_separation > max_separation) ? center_separation : max_separation;
            }

            if ((max_separation - min_separation) > tolerance) { // nonlinear increment
                return false;
            }

            total_separation += center_separation; // accumulate for mean
            ++num_separation;
        }
    }

    increment = total_separation / double(num_separation); // calculate mean separation
    return true;
}

double RegionHandler::GetSeparationTolerance(std::shared_ptr<casacore::CoordinateSystem> csys) {
    // Return 1% of CDELT2 in arcsec
    auto cdelt = csys->increment();
    auto cunit = csys->worldAxisUnits();
    casacore::Quantity cdelt2(cdelt[1], cunit[1]);
    return cdelt2.get("arcsec").getValue() * 0.01;
}

bool RegionHandler::GetFixedAngularRegionProfiles(int file_id, int width, bool per_z, RegionState& region_state,
    std::shared_ptr<casacore::CoordinateSystem> reference_csys, std::function<void(float)>& progress_callback,
    casacore::Matrix<float>& profiles, double& increment, bool& cancelled, std::string& message) {
    // Calculate mean spectral profiles for polygon regions along line with fixed angular spacing, with progress updates after each profile.
    // Return parameters include the profiles, the increment between the regions in arcsec, and whether profiles were cancelled.
    // Returns false if profiles cancelled or failed, with an error message.
    auto endpoints = region_state.control_points;
    auto rotation = region_state.rotation;

    // Convert pixel coordinates to MVDirection to get angular separation of entire line
    casacore::Vector<double> endpoint0(2), endpoint1(2);
    endpoint0[0] = endpoints[0].x();
    endpoint0[1] = endpoints[0].y();
    endpoint1[0] = endpoints[1].x();
    endpoint1[1] = endpoints[1].y();
    auto direction_coord = reference_csys->directionCoordinate();
    casacore::MVDirection mvdir0, mvdir1;

    try {
        mvdir0 = direction_coord.toWorld(endpoint0);
        mvdir1 = direction_coord.toWorld(endpoint1);
    } catch (casacore::AipsError& err) { // wcslib - invalid pixel coordinates
        message = "Conversion of line endpoints to world coordinates failed.";
        return false;
    }

    // Find angular center of line for start of line approximation regions
    auto line_separation = mvdir0.separation(mvdir1, "arcsec").getValue();
    auto center_separation = line_separation / 2.0; // angular separation of center from endpoint
    double tolerance = GetSeparationTolerance(reference_csys);
    casacore::Vector<double> line_center = FindPointAtTargetSeparation(direction_coord, endpoint0, endpoint1, center_separation, tolerance);

    if (line_center.empty()) { // Line may be outside image
        message = "Failed to find line center.  Region may be outside image.";
        return false;
    }

    // Target increment is CDELT2, target width is width * CDELT2
    auto inc2 = reference_csys->increment()(1);
    auto cunit2 = reference_csys->worldAxisUnits()(1);
    casacore::Quantity cdelt2(inc2, cunit2);
    increment = cdelt2.get("arcsec").getValue();
    double angular_width = width * increment;
    spdlog::debug("Increment={} arcsec, width={} arcsec", increment, angular_width);

    // Number of profiles determined by line length and increment in arcsec
    int num_offsets = lround(line_separation / increment) / 2;
    int num_profiles = num_offsets * 2;
    casacore::Vector<casacore::Vector<double>> line_points(num_profiles + 1);
    line_points(num_offsets) = line_center;
    spdlog::debug("Num offsets={} profiles={}", num_offsets, num_profiles);

    float progress(0.0);
    casacore::Vector<double> pos_box_start(line_center.copy()), neg_box_start(line_center.copy());

    // Get points along line from center out with increment spacing to set regions
    for (int i = 1; i <= num_offsets; ++i) {
        if (per_z && _stop_pv[file_id]) {
            cancelled = true;
            return false;
        }

        // Find ends of box regions, at increment from start of box in positive offset direction
        if (!pos_box_start.empty()) {
            casacore::Vector<double> pos_box_end =
                FindPointAtTargetSeparation(direction_coord, pos_box_start, endpoint0, increment, tolerance);
            line_points(num_offsets + i) = pos_box_end;
            pos_box_start.resize();
            pos_box_start = pos_box_end;
        }

        // Find ends of box regions, at increment from start of box in negative offset direction
        if (!neg_box_start.empty()) {
            casacore::Vector<double> neg_box_end =
                FindPointAtTargetSeparation(direction_coord, neg_box_start, endpoint1, increment, tolerance);
            line_points(num_offsets - i) = neg_box_end;
            neg_box_start.resize();
            neg_box_start = neg_box_end;
        }
    }

    // initialize matrix size to fill in rows
    profiles.resize(casacore::IPosition(2, num_profiles, _frames.at(file_id)->Depth()));
    profiles = NAN;

    // Create polygons for box regions along line starting and ending at calculated points.
    // Starts at previous point (if any), ends at two points ahead (if any).
    int start_point, end_point;

    // Send progress updates at time interval
    auto t_start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_profiles; ++i) {
        // Check if user cancelled
        if (per_z && _stop_pv[file_id]) {
            cancelled = true;
            return false;
        }

        // Set index for start of box and end of box
        start_point = (i == 0 ? i : i - 1);
        end_point = (i == (num_profiles - 1) ? i + 1 : i + 2);
        casacore::Vector<double> box_start(line_points(start_point)), box_end(line_points(end_point));

        if (box_start.empty() || box_end.empty()) {
            // Likely part of line off image
            spdlog::debug("Line box region {} max num pixels={}\n", i, "nan");
        } else {
            // Set temporary region for reference image and get profile for requested file_id
            RegionState temp_region_state = GetTemporaryRegionState(
                direction_coord, region_state.reference_file_id, box_start, box_end, width, angular_width, rotation, tolerance);

            double num_pixels(0.0);
            casacore::Vector<float> region_profile =
                GetTemporaryRegionProfile(file_id, temp_region_state, reference_csys, per_z, num_pixels);

            if (!num_pixels && per_z && _stop_pv[file_id]) {
                cancelled = true;
                return false;
            }

            spdlog::debug("Line box region {} max num pixels={}\n", i, num_pixels);

            profiles.row(i) = region_profile;
        }

        // Update progress if time interval elapsed
        auto t_end = std::chrono::high_resolution_clock::now();
        auto dt = std::chrono::duration<double, std::milli>(t_end - t_start).count();
        progress = float(i + 1) / float(num_profiles);

        if ((dt > LINE_PROFILE_PROGRESS_INTERVAL) || (progress == 1.0)) {
            t_start = t_end;
            progress_callback(progress);
        }
    }

    if (per_z) {
        cancelled = _stop_pv[file_id];
    }

    return (progress == 1.0) && !allEQ(profiles, NAN);
}

bool RegionHandler::SetPointInRange(float max_point, float& point) {
    // Sets point in range 0 to max_size; returns whether point was changed
    bool point_set(false);

    if (point < 0.0) {
        point = 0.0;
        point_set = true;
    } else if (point > max_point) {
        point = max_point;
        point_set = true;
    }

    return point_set;
}

casacore::Vector<double> RegionHandler::FindPointAtTargetSeparation(const casacore::DirectionCoordinate& direction_coord,
    const casacore::Vector<double>& start_point, const casacore::Vector<double>& end_point, double target_separation, double tolerance) {
    // Find point on line described by start and end points which is at target separation in arcsec (within tolerance) of start point.
    // Return point [x, y] in pixel coordinates.  Vector is empty if DirectionCoordinate conversion fails.
    casacore::Vector<double> target_point;

    // Do binary search of line, finding midpoints until target separation is reached.
    // Check endpoints
    casacore::MVDirection start = direction_coord.toWorld(start_point);
    casacore::MVDirection end = direction_coord.toWorld(end_point);
    auto separation = start.separation(end, "arcsec").getValue();

    if (separation < target_separation) {
        // Line is shorter than target separation
        return target_point;
    }

    // Set progressively smaller range end0-end1 which contains target point by testing midpoints
    casacore::Vector<double> end0(start_point.copy()), end1(end_point.copy()), last_end1(2), midpoint(2);
    int limit(0);
    auto delta = separation - target_separation;

    while (abs(delta) > tolerance) {
        if (limit++ == 1000) { // should not hit this
            break;
        }

        if (delta > 0) {
            // Separation too large, get midpoint of end0/end1
            midpoint[0] = (end0[0] + end1[0]) / 2;
            midpoint[1] = (end0[1] + end1[1]) / 2;

            last_end1 = end1.copy();
            end1 = midpoint.copy();
        } else {
            // Separation too small: get midpoint of end1/last_end1
            midpoint[0] = (end1[0] + last_end1[0]) / 2;
            midpoint[1] = (end1[1] + last_end1[1]) / 2;

            end0 = end1.copy();
            end1 = midpoint.copy();
        }

        // Get separation between start point and new endpoint
        casacore::MVDirection mvdir_end1 = direction_coord.toWorld(end1);
        separation = start.separation(mvdir_end1, "arcsec").getValue();
        delta = separation - target_separation;
    }

    if (abs(delta) <= tolerance) {
        target_point = end1.copy();
    }

    return target_point;
}

RegionState RegionHandler::GetTemporaryRegionState(casacore::DirectionCoordinate& direction_coord, int file_id,
    const casacore::Vector<double>& box_start, const casacore::Vector<double>& box_end, int pixel_width, double angular_width,
    float line_rotation, double tolerance) {
    // Return RegionState for polygon region describing a box with given start and end (pixel coords) on line with rotation.
    // Get box corners with angular width to get box corners.
    // Polygon control points are corners of this box.
    // Used for widefield images with nonlinear spacing, where pixel center is not angular center so cannot use rectangle definition.
    double half_width = angular_width / 2.0;
    float cos_x = cos(line_rotation * M_PI / 180.0f);
    float sin_x = sin(line_rotation * M_PI / 180.0f);

    // Control points for polygon region state
    std::vector<CARTA::Point> control_points(4);
    CARTA::Point point;

    // Create lines perpendicular to approximated line--along "width axis"--at box start and end to find box corners
    casacore::Vector<double> endpoint(2), corner(2);

    // Find box corners from box start
    // Endpoint in positive direction 3 pixels out from box start
    endpoint(0) = box_start(0) - (pixel_width * cos_x);
    endpoint(1) = box_start(1) - (pixel_width * sin_x);
    corner = FindPointAtTargetSeparation(direction_coord, box_start, endpoint, half_width, tolerance);
    point.set_x(corner(0));
    point.set_y(corner(1));
    control_points[0] = point;

    // Endpoint in negative direction 3 pixels out from box start
    endpoint(0) = box_start(0) + (pixel_width * cos_x);
    endpoint(1) = box_start(1) + (pixel_width * sin_x);
    corner = FindPointAtTargetSeparation(direction_coord, box_start, endpoint, half_width, tolerance);
    point.set_x(corner(0));
    point.set_y(corner(1));
    control_points[3] = point;

    // Find box corners from box end
    // Endpoint in positive direction 3 pixels out from box end
    endpoint(0) = box_end(0) - (pixel_width * cos_x);
    endpoint(1) = box_end(1) - (pixel_width * sin_x);
    corner = FindPointAtTargetSeparation(direction_coord, box_end, endpoint, half_width, tolerance);
    point.set_x(corner(0));
    point.set_y(corner(1));
    control_points[1] = point;

    // Endpoint in negative direction 3 pixels out from box end
    endpoint(0) = box_end(0) + (pixel_width * cos_x);
    endpoint(1) = box_end(1) + (pixel_width * sin_x);
    corner = FindPointAtTargetSeparation(direction_coord, box_end, endpoint, half_width, tolerance);
    point.set_x(corner(0));
    point.set_y(corner(1));
    control_points[2] = point;

    // Set polygon RegionState
    float polygon_rotation(0.0);
    RegionState region_state = RegionState(file_id, CARTA::RegionType::POLYGON, control_points, polygon_rotation);

    spdlog::debug("Angular box region corners: polygon[[{}pix, {}pix], [{}pix, {}pix], [{}pix, {}pix], [{}pix, {}pix]]",
        control_points[0].x(), control_points[0].y(), control_points[1].x(), control_points[1].y(), control_points[2].x(),
        control_points[2].y(), control_points[3].x(), control_points[3].y());

    return region_state;
}

casacore::Vector<float> RegionHandler::GetTemporaryRegionProfile(
    int file_id, RegionState& region_state, std::shared_ptr<casacore::CoordinateSystem> reference_csys, bool per_z, double& num_pixels) {
    // Create temporary region with RegionState and CoordinateSystem
    // Return stats/spectral profile (depending on per_z) for given file_id image, and number of pixels in the region.
    auto depth = _frames.at(file_id)->Depth();
    auto stokes_index = _frames.at(file_id)->CurrentStokes();

    // Initialize return values
    casacore::Vector<float> profile;
    if (per_z) {
        profile.resize(depth);
    } else {
        profile.resize(1);
    }
    profile = NAN;
    num_pixels = 0.0;

    if (!region_state.RegionDefined()) {
        return profile;
    }

    int region_id(TEMP_REGION_ID);
    SetRegion(region_id, region_state, reference_csys);

    if (!RegionSet(region_id)) {
        return profile;
    }

    std::vector<CARTA::StatsType> required_stats = {CARTA::StatsType::NumPixels, CARTA::StatsType::Mean};

    if (per_z) {
        // Temp region spectral requirements
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
        GetRegionSpectralData(region_id, file_id, coordinate, stokes_index, required_stats, report_error,
            [&](std::map<CARTA::StatsType, std::vector<double>> results, float progress) {
                if (progress == 1.0) {
                    // Get mean spectral profile and max NumPixels for small region.  Callback does nothing, not needed.
                    auto npix_per_chan = results[CARTA::StatsType::NumPixels];
                    num_pixels = *max_element(npix_per_chan.begin(), npix_per_chan.end());
                    profile = results[CARTA::StatsType::Mean];
                }
            });
    } else {
        CARTA::RegionStatsData stats_message;

        if (GetRegionStatsData(region_id, file_id, stokes_index, required_stats, stats_message)) {
            auto statistics = stats_message.statistics();
            for (auto& statistics_value : statistics) {
                if (statistics_value.stats_type() == CARTA::StatsType::NumPixels) {
                    num_pixels = statistics_value.value();
                } else if (statistics_value.stats_type() == CARTA::StatsType::Mean) {
                    profile[0] = statistics_value.value();
                }
            }
        }
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
