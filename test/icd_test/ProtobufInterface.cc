/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ProtobufInterface.h"

CARTA::RegisterViewer GetRegisterViewer(uint32_t session_id, string api_key, uint32_t client_feature_flags) {
    LogReceiveEventType(CARTA::EventType::REGISTER_VIEWER);

    CARTA::RegisterViewer register_viewer;
    register_viewer.set_session_id(session_id);
    register_viewer.set_api_key(api_key);
    register_viewer.set_client_feature_flags(client_feature_flags);
    return register_viewer;
}

CARTA::CloseFile GetCloseFile(int32_t file_id) {
    LogReceiveEventType(CARTA::EventType::CLOSE_FILE);

    CARTA::CloseFile close_file;
    close_file.set_file_id(file_id);
    return close_file;
}

CARTA::OpenFile GetOpenFile(string directory, string file, string hdu, int32_t file_id, CARTA::RenderMode render_mode) {
    LogReceiveEventType(CARTA::EventType::OPEN_FILE);

    CARTA::OpenFile open_file;
    open_file.set_directory(directory);
    open_file.set_file(file);
    open_file.set_hdu(hdu);
    open_file.set_file_id(file_id);
    open_file.set_render_mode(render_mode);
    return open_file;
}

CARTA::SetImageChannels GetSetImageChannels(
    int32_t file_id, int32_t channel, int32_t stokes, CARTA::CompressionType compression_type, float compression_quality) {
    LogReceiveEventType(CARTA::EventType::SET_IMAGE_CHANNELS);

    CARTA::SetImageChannels set_image_channels;
    set_image_channels.set_file_id(file_id);
    set_image_channels.set_channel(channel);
    set_image_channels.set_stokes(stokes);
    CARTA::AddRequiredTiles* required_tiles = set_image_channels.mutable_required_tiles();
    required_tiles->set_file_id(file_id);
    required_tiles->set_compression_type(compression_type);
    required_tiles->set_compression_quality(compression_quality);
    required_tiles->add_tiles(0);
    return set_image_channels;
}

CARTA::SetCursor GetSetCursor(int32_t file_id, float x, float y) {
    LogReceiveEventType(CARTA::EventType::SET_CURSOR);

    CARTA::SetCursor set_cursor;
    set_cursor.set_file_id(file_id);
    auto* point = set_cursor.mutable_point();
    point->set_x(x);
    point->set_y(y);
    return set_cursor;
}

CARTA::SetSpatialRequirements GetSetSpatialRequirements(int32_t file_id, int32_t region_id) {
    LogReceiveEventType(CARTA::EventType::SET_SPATIAL_REQUIREMENTS);

    CARTA::SetSpatialRequirements set_spatial_requirements;
    set_spatial_requirements.set_file_id(file_id);
    set_spatial_requirements.set_region_id(region_id);
    auto* spatial_requirement_x = set_spatial_requirements.add_spatial_profiles();
    spatial_requirement_x->set_coordinate("x");
    auto* spatial_requirement_y = set_spatial_requirements.add_spatial_profiles();
    spatial_requirement_y->set_coordinate("y");
    return set_spatial_requirements;
}

CARTA::SetStatsRequirements GetSetStatsRequirements(int32_t file_id, int32_t region_id) {
    LogReceiveEventType(CARTA::EventType::SET_STATS_REQUIREMENTS);

    CARTA::SetStatsRequirements set_stats_requirements;
    set_stats_requirements.set_file_id(file_id);
    set_stats_requirements.set_region_id(region_id);
    auto* stats_config = set_stats_requirements.add_stats_configs();
    stats_config->add_stats_types(CARTA::StatsType::NumPixels);
    stats_config->add_stats_types(CARTA::StatsType::Sum);
    stats_config->add_stats_types(CARTA::StatsType::Mean);
    stats_config->add_stats_types(CARTA::StatsType::RMS);
    stats_config->add_stats_types(CARTA::StatsType::Sigma);
    stats_config->add_stats_types(CARTA::StatsType::SumSq);
    stats_config->add_stats_types(CARTA::StatsType::Min);
    stats_config->add_stats_types(CARTA::StatsType::Max);
    return set_stats_requirements;
}

CARTA::SetHistogramRequirements GetSetHistogramRequirements(int32_t file_id, int32_t region_id) {
    LogReceiveEventType(CARTA::EventType::SET_HISTOGRAM_REQUIREMENTS);

    CARTA::SetHistogramRequirements set_histogram_requirements;
    set_histogram_requirements.set_file_id(file_id);
    set_histogram_requirements.set_region_id(region_id);
    auto* histograms = set_histogram_requirements.add_histograms();
    histograms->set_channel(-1);
    histograms->set_num_bins(-1);
    return set_histogram_requirements;
}

CARTA::AddRequiredTiles GetAddRequiredTiles(int32_t file_id, CARTA::CompressionType compression_type, float compression_quality) {
    LogReceiveEventType(CARTA::EventType::ADD_REQUIRED_TILES);

    CARTA::AddRequiredTiles add_required_tiles;
    add_required_tiles.set_file_id(file_id);
    add_required_tiles.set_compression_type(compression_type);
    add_required_tiles.set_compression_quality(compression_quality);
    add_required_tiles.add_tiles(0);
    return add_required_tiles;
}

CARTA::Point GetPoint(int x, int y) {
    CARTA::Point point;
    point.set_x(x);
    point.set_y(y);
    return point;
}

CARTA::SetRegion GetSetRegion(
    int32_t file_id, int32_t region_id, CARTA::RegionType region_type, vector<CARTA::Point> control_points, float rotation) {
    LogReceiveEventType(CARTA::EventType::SET_REGION);

    CARTA::SetRegion set_region;
    set_region.set_file_id(file_id);
    set_region.set_region_id(region_id);
    auto* region_info = set_region.mutable_region_info();
    region_info->set_region_type(region_type);
    region_info->set_rotation(rotation);
    for (auto control_point : control_points) {
        auto* point = region_info->add_control_points();
        point->set_x(control_point.x());
        point->set_y(control_point.y());
    }
    return set_region;
}

CARTA::SetStatsRequirements GetSetStatsRequirements(int32_t file_id, int32_t region_id, string coordinate) {
    LogReceiveEventType(CARTA::EventType::SET_STATS_REQUIREMENTS);

    CARTA::SetStatsRequirements set_stats_requirements;
    set_stats_requirements.set_file_id(file_id);
    set_stats_requirements.set_region_id(region_id);
    auto* stats_configs = set_stats_requirements.add_stats_configs();
    stats_configs->set_coordinate(coordinate);
    stats_configs->add_stats_types(CARTA::StatsType::NumPixels);
    stats_configs->add_stats_types(CARTA::StatsType::Sum);
    stats_configs->add_stats_types(CARTA::StatsType::FluxDensity);
    stats_configs->add_stats_types(CARTA::StatsType::Mean);
    stats_configs->add_stats_types(CARTA::StatsType::RMS);
    stats_configs->add_stats_types(CARTA::StatsType::Sigma);
    stats_configs->add_stats_types(CARTA::StatsType::SumSq);
    stats_configs->add_stats_types(CARTA::StatsType::Min);
    stats_configs->add_stats_types(CARTA::StatsType::Max);
    stats_configs->add_stats_types(CARTA::StatsType::Extrema);
    return set_stats_requirements;
}

CARTA::EventType GetEventType(std::vector<char>& message) {
    carta::EventHeader head = *reinterpret_cast<const carta::EventHeader*>(message.data());
    return static_cast<CARTA::EventType>(head.type);
}

void LogReceiveEventType(const CARTA::EventType& event_type) {
    spdlog::info("==> {}", EventType_Name(event_type));
}

void LogResponseEventType(const CARTA::EventType& event_type) {
    spdlog::info("<== {}", EventType_Name(event_type));
}