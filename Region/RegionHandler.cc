/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// RegionDataHandler.cc: handle requirements and data streams for regions

#include "RegionHandler.h"

#include <chrono>

#include <casacore/lattices/LRegions/LCBox.h>
#include <casacore/lattices/LRegions/LCExtension.h>
#include <casacore/lattices/LRegions/LCIntersection.h>

#include "../ImageStats/StatsCalculator.h"
#include "../InterfaceConstants.h"
#include "../Util.h"
#include "CrtfImportExport.h"
#include "Ds9ImportExport.h"

namespace carta {

RegionHandler::RegionHandler(bool perflog) : _perflog(perflog), _z_profile_count(0) {}

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

bool RegionHandler::SetRegion(int& region_id, RegionState& region_state, casacore::CoordinateSystem* csys) {
    // Set region params for region id; if id < 0, create new id
    bool valid_region(false);
    if (_regions.count(region_id)) {
        _regions.at(region_id)->UpdateRegion(region_state);
        valid_region = _regions.at(region_id)->IsValid();
        if (_regions.at(region_id)->RegionChanged()) {
            UpdateNewSpectralRequirements(region_id); // set all req "new"
            ClearRegionCache(region_id);
        }
    } else {
        if (region_id < 0) {
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

    // Importer must delete csys pointer
    casacore::CoordinateSystem* csys = frame->CoordinateSystem();
    const casacore::IPosition shape = frame->ImageShape();
    std::unique_ptr<RegionImportExport> importer;
    switch (region_file_type) {
        case CARTA::FileType::CRTF:
            importer = std::unique_ptr<RegionImportExport>(
                new CrtfImportExport(csys, shape, frame->StokesAxis(), file_id, region_file, file_is_filename));
            break;
        case CARTA::FileType::DS9_REG:
            importer = std::unique_ptr<RegionImportExport>(new Ds9ImportExport(csys, shape, file_id, region_file, file_is_filename));
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

    // Exporter must delete csys pointer
    casacore::CoordinateSystem* output_csys = frame->CoordinateSystem();

    if (!pixel_coord && !output_csys->hasDirectionCoordinate()) {
        // Export failed
        delete output_csys;
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

            // Use RegionState control points with reference file id for pixel export
            if ((region_state.reference_file_id == file_id) && pixel_coord) {
                region_added = exporter->AddExportRegion(region_state, region_style);
            } else {
                try {
                    // Get converted lattice region parameters (pixel) as a Record
                    casacore::TableRecord region_record = _regions.at(region_id)->GetImageRegionRecord(file_id, *output_csys, output_shape);
                    if (!region_record.empty()) {
                        region_added = exporter->AddExportRegion(region_state, region_style, region_record, pixel_coord);
                    }
                } catch (const casacore::AipsError& err) {
                    std::cerr << "Export region record failed: " << err.getMesg() << std::endl;
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

    if (!_regions.count(region_id)) {
        std::cerr << "Spectral requirements failed: no region with id " << region_id << std::endl;
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
        if (!SpectralCoordinateValid(profile_coordinate, nstokes)) {
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

bool RegionHandler::SpectralCoordinateValid(std::string& coordinate, int nstokes) {
    // Check stokes coordinate is valid for image
    int axis_index, stokes_index;
    ConvertCoordinateToAxes(coordinate, axis_index, stokes_index);
    bool valid(stokes_index < nstokes);
    if (!valid) {
        std::cerr << "Spectral requirement " << coordinate << " failed: invalid stokes axis for image." << std::endl;
    }
    return valid;
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

casacore::LCRegion* RegionHandler::ApplyRegionToFile(int region_id, int file_id) {
    // Returns 2D region with no extension; nullptr if outside image
    // Go through Frame for image mutex
    return _frames.at(file_id)->GetImageRegion(file_id, _regions.at(region_id));
}

bool RegionHandler::ApplyRegionToFile(
    int region_id, int file_id, const ChannelRange& chan_range, int stokes, casacore::ImageRegion& region) {
    // Returns 3D or 4D image region for region applied to image and extended by channel range and stokes
    if (!RegionSet(region_id) || !FrameSet(file_id)) {
        return false;
    }

    try {
        casacore::LCRegion* applied_region = ApplyRegionToFile(region_id, file_id);
        casacore::IPosition image_shape(_frames.at(file_id)->ImageShape());
        if (applied_region == nullptr) {
            return false;
        }

        // Create LCBox with chan range and stokes using a slicer
        casacore::Slicer chan_stokes_slicer = _frames.at(file_id)->GetImageSlicer(chan_range, stokes);
        casacore::LCBox chan_stokes_box(chan_stokes_slicer, image_shape);

        // Set returned region
        // Combine applied region with chan/stokes box
        if (applied_region->shape().size() == image_shape.size()) {
            // Intersection combines applied_region xy limits and box chan/stokes limits
            casacore::LCBox chan_stokes_box(chan_stokes_slicer, image_shape);
            casacore::LCIntersection final_region(*applied_region, chan_stokes_box);
            region = casacore::ImageRegion(final_region);
        } else {
            // Extension extends applied_region in xy axes by chan/stokes axes only
            // Remove xy axes from chan/stokes box
            casacore::IPosition remove_xy(2, 0, 1);
            chan_stokes_slicer =
                casacore::Slicer(chan_stokes_slicer.start().removeAxes(remove_xy), chan_stokes_slicer.length().removeAxes(remove_xy));
            casacore::LCBox chan_stokes_box(chan_stokes_slicer, image_shape.removeAxes(remove_xy));

            casacore::IPosition extend_axes = casacore::IPosition::makeAxisPath(image_shape.size()).removeAxes(remove_xy);
            casacore::LCExtension final_region(*applied_region, extend_axes, chan_stokes_box);
            region = casacore::ImageRegion(final_region);
        }

        return true;
    } catch (const casacore::AipsError& err) {
        std::cerr << "Error applying region " << region_id << " to file " << file_id << ": " << err.getMesg() << std::endl;
    } catch (std::out_of_range& range_error) {
        std::cerr << "Cannot apply region " << region_id << " to closed file " << file_id << std::endl;
    }

    return false;
}

bool RegionHandler::CalculateMoments(int file_id, int region_id, const std::shared_ptr<Frame>& frame,
    MomentProgressCallback progress_callback, const CARTA::MomentRequest& moment_request, CARTA::MomentResponse& moment_response,
    std::vector<carta::CollapseResult>& collapse_results) {
    casacore::ImageRegion image_region;
    int chan_min(moment_request.spectral_range().min());
    int chan_max(moment_request.spectral_range().max());

    // Do calculations
    if (ApplyRegionToFile(region_id, file_id, ChannelRange(chan_min, chan_max), frame->CurrentStokes(), image_region)) {
        frame->IncreaseMomentsCount();
        frame->CalculateMoments(file_id, progress_callback, image_region, moment_request, moment_response, collapse_results);
        frame->DecreaseMomentsCount();
    }
    return !collapse_results.empty();
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

    if (_perflog) {
        auto t_end_region_histogram = std::chrono::high_resolution_clock::now();
        auto dt_region_histogram =
            std::chrono::duration_cast<std::chrono::microseconds>(t_end_region_histogram - t_start_region_histogram).count();
        fmt::print(
            "Fill region histogram in {} ms at {} MPix/s\n", dt_region_histogram * 1e-3, (float)stats.num_pixels / dt_region_histogram);
    }

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

                // Get index into stokes axis for data message
                int axis_index, stokes_index;
                ConvertCoordinateToAxes(coordinate, axis_index, stokes_index);
                if (stokes_index < 0) {
                    stokes_index = _frames.at(config_file_id)->CurrentStokes();
                }

                // Return spectral profile for this requirement
                profile_ok = GetRegionSpectralData(config_region_id, config_file_id, coordinate, stokes_index, required_stats,
                    [&](std::map<CARTA::StatsType, std::vector<double>> results, float progress) {
                        CARTA::SpectralProfileData profile_message;
                        profile_message.set_file_id(config_file_id);
                        profile_message.set_region_id(config_region_id);
                        profile_message.set_stokes(stokes_index);
                        profile_message.set_progress(progress);
                        FillSpectralProfileDataMessage(profile_message, coordinate, required_stats, results);
                        cb(profile_message); // send (partial profile) data
                    });
            }
        }
    }

    return profile_ok;
}

bool RegionHandler::GetRegionSpectralData(int region_id, int file_id, std::string& coordinate, int stokes_index,
    std::vector<CARTA::StatsType>& required_stats,
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
    _frames.at(file_id)->IncreaseZProfileCount();
    _regions.at(region_id)->IncreaseZProfileCount();

    // Initialize results map for requested stats to NaN, progress to zero
    size_t profile_size = _frames.at(file_id)->NumChannels();
    std::vector<double> init_spectral(profile_size, std::numeric_limits<double>::quiet_NaN());
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
        progress = PROFILE_COMPLETE;
        partial_results_callback(results, progress);

        _frames.at(file_id)->DecreaseZProfileCount();
        _regions.at(region_id)->DecreaseZProfileCount();
        return true;
    }

    // Get region to check if inside image
    casacore::LCRegion* lcregion = ApplyRegionToFile(region_id, file_id);
    if (!lcregion) {
        progress = PROFILE_COMPLETE;
        partial_results_callback(results, progress); // region outside image, send NaNs
        _frames.at(file_id)->DecreaseZProfileCount();
        _regions.at(region_id)->DecreaseZProfileCount();
        return true;
    }

    // Get initial region info to cancel profile if it changes
    RegionState initial_region_state = _regions.at(region_id)->GetRegionState();

    // Use loader swizzled data for efficiency
    if (_frames.at(file_id)->UseLoaderSpectralData(lcregion->shape())) {
        // Use cursor spectral profile for point region
        if (initial_region_state.type == CARTA::RegionType::POINT) {
            casacore::IPosition origin = lcregion->boundingBox().start();
            CARTA::Point point;
            point.set_x(origin(0));
            point.set_y(origin(1));
            std::vector<float> profile;
            bool ok = _frames.at(file_id)->GetLoaderPointSpectralData(profile, stokes_index, point);
            if (ok) {
                // Set results; there is only one required stat for point
                std::vector<double> data(profile.begin(), profile.end());
                results[required_stats[0]] = data;
                progress = PROFILE_COMPLETE;
                partial_results_callback(results, progress);
            }

            _frames.at(file_id)->DecreaseZProfileCount();
            _regions.at(region_id)->DecreaseZProfileCount();
            return ok;
        }

        // Get 2D origin and 2D mask for Hdf5Loader
        casacore::IPosition origin = lcregion->boundingBox().start();
        casacore::IPosition xy_origin = origin.keepAxes(casacore::IPosition(2, 0, 1)); // keep first two axes only

        // Get mask; LCRegion for file id is cached
        casacore::ArrayLattice<casacore::Bool> mask = _regions[region_id]->GetImageRegionMask(file_id);
        if (!mask.shape().empty()) {
            // start the timer
            auto t_start = std::chrono::high_resolution_clock::now();
            auto t_latest = t_start;

            // Get partial profiles until complete (do once if cached)
            while (progress < PROFILE_COMPLETE) {
                // Cancel if region or frame is closing
                if (!RegionFileIdsValid(region_id, file_id)) {
                    _frames.at(file_id)->DecreaseZProfileCount();
                    _regions.at(region_id)->DecreaseZProfileCount();
                    return false;
                }

                // Cancel if region, current stokes, or spectral requirements changed
                if (_regions.at(region_id)->GetRegionState() != initial_region_state) {
                    _frames.at(file_id)->DecreaseZProfileCount();
                    _regions.at(region_id)->DecreaseZProfileCount();
                    return false;
                }
                if (use_current_stokes && (stokes_index != _frames.at(file_id)->CurrentStokes())) {
                    _frames.at(file_id)->DecreaseZProfileCount();
                    _regions.at(region_id)->DecreaseZProfileCount();
                    return false;
                }
                if (!HasSpectralRequirements(region_id, file_id, coordinate, required_stats)) {
                    _frames.at(file_id)->DecreaseZProfileCount();
                    _regions.at(region_id)->DecreaseZProfileCount();
                    return false;
                }

                // Get partial profile
                std::map<CARTA::StatsType, std::vector<double>> partial_profiles;
                if (_frames.at(file_id)->GetLoaderSpectralData(region_id, stokes_index, mask, xy_origin, partial_profiles, progress)) {
                    // get the time elapse for this step
                    auto t_end = std::chrono::high_resolution_clock::now();
                    auto dt = std::chrono::duration<double, std::milli>(t_end - t_latest).count();

                    if ((dt > TARGET_PARTIAL_REGION_TIME) || (progress >= PROFILE_COMPLETE)) {
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
                } else {
                    _frames.at(file_id)->DecreaseZProfileCount();
                    _regions.at(region_id)->DecreaseZProfileCount();
                    return false;
                }
            }

            if (_perflog) {
                auto t_end_spectral_profile = std::chrono::high_resolution_clock::now();
                auto dt_spectral_profile =
                    std::chrono::duration_cast<std::chrono::microseconds>(t_end_spectral_profile - t_start_spectral_profile).count();
                fmt::print("Fill spectral profile in {} ms\n", dt_spectral_profile * 1e-3);
            }

            _frames.at(file_id)->DecreaseZProfileCount();
            _regions.at(region_id)->DecreaseZProfileCount();
            return true;
        }
    } // end loader swizzled data

    // Initialize cache results for *all* spectral stats
    std::map<CARTA::StatsType, std::vector<double>> cache_results;
    for (const auto& stat : _spectral_stats) {
        cache_results[stat] = init_spectral;
    }

    // Calculate and cache profiles
    size_t start_channel(0), count(0), end_channel(0);
    int delta_channels = INIT_DELTA_CHANNEL; // the increment of channels for each step
    int dt_target = TARGET_DELTA_TIME;       // the target time elapse for each step, in the unit of milliseconds
    auto t_partial_profile_start = std::chrono::high_resolution_clock::now();

    // get stats data
    while (progress < PROFILE_COMPLETE) {
        // start the timer
        auto t_start = std::chrono::high_resolution_clock::now();

        end_channel = (start_channel + delta_channels > profile_size ? profile_size - 1 : start_channel + delta_channels - 1);
        count = end_channel - start_channel + 1;

        // Get region for channel range only and stokes_index
        ChannelRange chan_range(start_channel, end_channel);
        casacore::ImageRegion region;
        if (!ApplyRegionToFile(region_id, file_id, chan_range, stokes_index, region)) {
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

        // Cancel if region or frame is closing
        if (!RegionFileIdsValid(region_id, file_id)) {
            _frames.at(file_id)->DecreaseZProfileCount();
            _regions.at(region_id)->DecreaseZProfileCount();
            return false;
        }
        // Cancel if region, current stokes, or spectral requirements changed
        if (_regions.at(region_id)->GetRegionState() != initial_region_state) {
            _frames.at(file_id)->DecreaseZProfileCount();
            _regions.at(region_id)->DecreaseZProfileCount();
            return false;
        }
        if (use_current_stokes && (stokes_index != _frames.at(file_id)->CurrentStokes())) {
            _frames.at(file_id)->DecreaseZProfileCount();
            _regions.at(region_id)->DecreaseZProfileCount();
            return false;
        }
        if (!HasSpectralRequirements(region_id, file_id, coordinate, required_stats)) {
            _frames.at(file_id)->DecreaseZProfileCount();
            _regions.at(region_id)->DecreaseZProfileCount();
            return false;
        }

        // send partial result by the callback function
        if (dt_partial_profile > TARGET_PARTIAL_REGION_TIME || progress >= PROFILE_COMPLETE) {
            t_partial_profile_start = std::chrono::high_resolution_clock::now();
            partial_results_callback(results, progress);
            if (progress >= PROFILE_COMPLETE) {
                // cache results for all stats types
                // TODO: cache and load partial profiles
                _spectral_cache[cache_id] = SpectralCache(cache_results);
            }
        }
    }

    if (_perflog) {
        auto t_end_spectral_profile = std::chrono::high_resolution_clock::now();
        auto dt_spectral_profile =
            std::chrono::duration_cast<std::chrono::microseconds>(t_end_spectral_profile - t_start_spectral_profile).count();
        fmt::print("Fill spectral profile in {} ms\n", dt_spectral_profile * 1e-3);
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

        if (_perflog) {
            auto t_end_region_stats = std::chrono::high_resolution_clock::now();
            auto dt_region_stats = std::chrono::duration_cast<std::chrono::microseconds>(t_end_region_stats - t_start_region_stats).count();
            fmt::print("Fill region stats in {} ms\n", dt_region_stats * 1e-3);
        }
        return true;
    }

    return false;
}

} // namespace carta