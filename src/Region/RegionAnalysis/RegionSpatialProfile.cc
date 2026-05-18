/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// RegionSpatialProfile.cc: calculate spatial profiles for point or line in a frame.

#include "RegionSpatialProfile.h"

#include "ImageStats/StatsCalculator.h"
#include "LineBoxRegions.h"
#include "Region/ImageRegion.h"
#include "Util/Nan.h"

namespace carta {

RegionSpatialProfile::RegionSpatialProfile(
    int region_id, int file_id, const std::vector<CARTA::SetSpatialRequirements_SpatialConfig>& configs)
    : _region_id(region_id) {
    SetConfigurations(file_id, configs);
}

void RegionSpatialProfile::SetConfigurations(int file_id, const std::vector<CARTA::SetSpatialRequirements_SpatialConfig>& configs) {
    ConfigId config_id(file_id, _region_id);
    std::lock_guard<std::mutex> guard(_config_mutex);
    _configs[config_id] = configs;
}

bool RegionSpatialProfile::GetConfigurations(int file_id, std::vector<CARTA::SetSpatialRequirements_SpatialConfig>& configs) {
    ConfigId config_id(file_id, _region_id);
    std::lock_guard<std::mutex> guard(_config_mutex);
    if (_configs.find(config_id) != _configs.end()) {
        configs = _configs[config_id];
        return true;
    }
    return false;
}

bool RegionSpatialProfile::HasConfiguration(int file_id, CARTA::SetSpatialRequirements_SpatialConfig& config) {
    std::vector<CARTA::SetSpatialRequirements_SpatialConfig> file_configs;
    if (!GetConfigurations(file_id, file_configs)) {
        return false;
    }

    auto coordinate(config.coordinate());
    auto width(config.width());
    bool found(false);

    for (auto& file_config : file_configs) {
        if ((file_config.coordinate() == coordinate) && (file_config.width() == width)) {
            found = true;
            break;
        }
    }
    return found;
}

std::vector<int> RegionSpatialProfile::GetConfigFileIds(int file_id) {
    std::vector<int> file_ids;
    std::lock_guard<std::mutex> guard(_config_mutex);
    for (const auto& [config_id, _] : _configs) {
        // File id -1 is for all files
        if ((file_id < 0) || (config_id.file_id == file_id)) {
            file_ids.push_back(config_id.file_id);
        }
    }
    return file_ids;
}

void RegionSpatialProfile::ClearFileConfigs(int file_id) {
    std::lock_guard<std::mutex> guard(_config_mutex);
    for (auto it = _configs.begin(); it != _configs.end();) {
        if ((*it).first.file_id == file_id) {
            it = _configs.erase(it);
        } else {
            ++it;
        }
    }
}

bool RegionSpatialProfile::GetPointSpatialProfile(int file_id, std::shared_ptr<Frame> frame,
    std::vector<CARTA::SetSpatialRequirements_SpatialConfig>& configs, std::shared_ptr<casacore::LCRegion> lc_region,
    std::vector<CARTA::SpatialProfileData>& spatial_profile_messages) {
    if (!lc_region) {
        return false;
    }

    // Get point region
    casacore::IPosition origin = lc_region->boundingBox().start();
    PointXy point(origin(0), origin(1));

    // Get profiles for point region
    if (!frame->FillSpatialProfileData(point, configs, spatial_profile_messages)) {
        return false;
    }

    // Complete messages
    for (auto& message : spatial_profile_messages) {
        message.set_file_id(file_id);
        message.set_region_id(_region_id);
    }

    return true;
}

bool RegionSpatialProfile::GetLineSpatialProfile(int file_id, std::shared_ptr<Frame> frame, std::shared_ptr<Region> region,
    int stokes_index, int z, CARTA::SetSpatialRequirements_SpatialConfig& config, bool& cancelled, std::string& message,
    CARTA::SpatialProfileData& spatial_profile_message) {
    int width(config.width());
    std::string coordinate(config.coordinate());

    if (width < 1 || width > 20) {
        message = fmt::format("Invalid averaging width: {}.", width);
        spdlog::error(message);
        return false;
    }

    CARTA::ProfileAxisType axis_type =
        (region->GetRegionState().type == CARTA::LINE ? CARTA::ProfileAxisType::Offset : CARTA::ProfileAxisType::Distance);

    std::vector<float> profile;
    casacore::Quantity increment;
    if (!GetLineProfile(file_id, frame, region, stokes_index, z, config, cancelled, message, profile, increment)) {
        return false;
    }

    auto profile_size = profile.size();

    // Set message fields
    // x, y, value for cursor/point only
    int x(0), y(0), start(0), mip(0);
    float value(0.0);
    int end = profile_size - 1;
    float crpix = profile_size / 2;
    float cdelt = increment.getValue();
    float crval = (axis_type == CARTA::ProfileAxisType::Offset ? 0.0 : crpix * cdelt);
    std::string unit = increment.getUnit();

    // Set message return value
    spatial_profile_message = Message::SpatialProfileData(file_id, _region_id, x, y, z, stokes_index, value);
    auto spatial_profile = Message::AddProfile(spatial_profile_message, start, end, profile, coordinate, mip);
    Message::AddLineProfileAxis(spatial_profile, axis_type, crpix, crval, cdelt, unit);
    return true;
}

bool RegionSpatialProfile::GetLineProfile(int file_id, std::shared_ptr<Frame> frame, std::shared_ptr<Region> region, int stokes_index,
    int z, CARTA::SetSpatialRequirements_SpatialConfig& config, bool& cancelled, std::string& message, std::vector<float>& profile,
    casacore::Quantity& increment) {
    std::shared_lock region_lock(region->GetActiveTaskMutex());
    auto initial_region_state = region->GetRegionState();
    auto region_csys = region->CoordinateSystem();
    region_lock.unlock();

    if (CancelLineProfile(file_id, frame, region, z, initial_region_state, config)) {
        cancelled = true;
        return false;
    }

    // Get line approximated as series of box regions (returned as RegionState vector) and the increment between them.
    LineBoxRegions line_box_regions;
    std::vector<RegionState> box_regions;
    if (!line_box_regions.GetLineBoxRegions(initial_region_state, region_csys, config.width(), increment, box_regions, message)) {
        return false;
    }

    for (auto& box_region : box_regions) {
        // Check cancellation
        if (CancelLineProfile(file_id, frame, region, z, initial_region_state, config)) {
            profile.clear();
            cancelled = true;
            return false;
        }

        // Set mean for each box region in profile
        profile.push_back(GetBoxMeanValue(file_id, frame, box_region, region_csys, z, stokes_index));
    }

    if (CancelLineProfile(file_id, frame, region, z, initial_region_state, config)) {
        profile.clear();
        cancelled = true;
        return false;
    }

    return true;
}

float RegionSpatialProfile::GetBoxMeanValue(int file_id, std::shared_ptr<Frame> frame, RegionState& region_state,
    std::shared_ptr<casacore::CoordinateSystem> coord_sys, int z, int stokes_index) {
    if (!region_state.RegionDefined()) {
        return FLOAT_NAN;
    }

    // Set box Region
    std::shared_ptr<Region> box_region = std::make_shared<Region>(region_state, coord_sys);
    StokesSource stokes_source(stokes_index, AxisRange(z));

    // Get box LCRegion
    std::shared_lock frame_lock(frame->GetActiveTaskMutex());
    auto box_lc_region =
        box_region->GetLCRegion(file_id, frame->CoordinateSystem(stokes_source), frame->ImageShape(stokes_source), stokes_source);

    if (!box_lc_region) {
        return FLOAT_NAN;
    }

    // Get box ImageRegion
    casacore::ImageRegion image_region;
    if (!GetImageRegion(box_region, frame, AxisRange(z), stokes_index, box_lc_region, image_region)) {
        return FLOAT_NAN;
    }

    // Get data from box ImageRegion
    StokesRegion stokes_region(stokes_source, image_region);
    std::vector<float> region_data;
    if (!frame->GetRegionData(stokes_region, region_data)) {
        return FLOAT_NAN;
    }

    frame_lock.unlock();

    // Calculate basic stats and return mean
    BasicStats<float> stats;
    CalcBasicStats(stats, region_data.data(), region_data.size());
    float box_mean = stats.mean;
    return box_mean;
}

bool RegionSpatialProfile::CancelLineProfile(int file_id, std::shared_ptr<Frame> frame, std::shared_ptr<Region> line_region, int z,
    RegionState& region_state, CARTA::SetSpatialRequirements_SpatialConfig& config) {
    std::shared_lock frame_lock(frame->GetActiveTaskMutex());
    if (!frame->IsConnected() || frame->CurrentZ() != z) {
        return true;
    }
    frame_lock.unlock();

    std::shared_lock region_lock(line_region->GetActiveTaskMutex());
    if (!line_region->IsConnected() || line_region->GetRegionState() != region_state) {
        return true;
    }
    region_lock.unlock();

    // Check if configuration was removed
    if (!HasConfiguration(file_id, config)) {
        return true;
    }

    return false;
}

} // namespace carta
