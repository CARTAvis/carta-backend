/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ProtobufInterface.h"

CARTA::RegisterViewer GetRegisterViewer(uint32_t session_id, string api_key, uint32_t client_feature_flags) {
    CARTA::RegisterViewer register_viewer;
    register_viewer.set_session_id(session_id);
    register_viewer.set_api_key(api_key);
    register_viewer.set_client_feature_flags(client_feature_flags);
    return register_viewer;
}

CARTA::CloseFile GetCloseFile(int32_t file_id) {
    CARTA::CloseFile close_file;
    close_file.set_file_id(file_id);
    return close_file;
}

CARTA::OpenFile GetOpenFile(string directory, string file, string hdu, int32_t file_id, CARTA::RenderMode render_mode) {
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
    CARTA::SetCursor set_cursor;
    set_cursor.set_file_id(file_id);
    auto* point = set_cursor.mutable_point();
    point->set_x(x);
    point->set_y(y);
    return set_cursor;
}

CARTA::SetSpatialRequirements GetSetSpatialRequirements(int32_t file_id, int32_t region_id) {
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

CARTA::SetHistogramRequirements GetSetHistogramRequirements(int32_t file_id, int32_t region_id, int32_t channel, int32_t num_bins) {
    CARTA::SetHistogramRequirements set_histogram_requirements;
    set_histogram_requirements.set_file_id(file_id);
    set_histogram_requirements.set_region_id(region_id);
    auto* histograms = set_histogram_requirements.add_histograms();
    histograms->set_channel(channel);
    histograms->set_num_bins(num_bins);
    return set_histogram_requirements;
}

CARTA::AddRequiredTiles GetAddRequiredTiles(
    int32_t file_id, CARTA::CompressionType compression_type, float compression_quality, const std::vector<float>& tiles) {
    CARTA::AddRequiredTiles add_required_tiles;
    add_required_tiles.set_file_id(file_id);
    add_required_tiles.set_compression_type(compression_type);
    add_required_tiles.set_compression_quality(compression_quality);
    for (int i = 0; i < tiles.size(); ++i) {
        add_required_tiles.add_tiles(tiles[i]);
    }
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

CARTA::SetSpectralRequirements GetSetSpectralRequirements(int32_t file_id, int32_t region_id, string coordinate) {
    CARTA::SetSpectralRequirements set_spectral_requirements;
    set_spectral_requirements.set_file_id(file_id);
    set_spectral_requirements.set_region_id(region_id);
    auto* spectral_profiles = set_spectral_requirements.add_spectral_profiles();
    spectral_profiles->set_coordinate(coordinate);
    spectral_profiles->add_stats_types(CARTA::StatsType::NumPixels);
    spectral_profiles->add_stats_types(CARTA::StatsType::Sum);
    spectral_profiles->add_stats_types(CARTA::StatsType::FluxDensity);
    spectral_profiles->add_stats_types(CARTA::StatsType::Mean);
    spectral_profiles->add_stats_types(CARTA::StatsType::RMS);
    spectral_profiles->add_stats_types(CARTA::StatsType::Sigma);
    spectral_profiles->add_stats_types(CARTA::StatsType::SumSq);
    spectral_profiles->add_stats_types(CARTA::StatsType::Min);
    spectral_profiles->add_stats_types(CARTA::StatsType::Max);
    spectral_profiles->add_stats_types(CARTA::StatsType::Extrema);
    return set_spectral_requirements;
}

CARTA::StartAnimation GetStartAnimation(int32_t file_id, std::pair<int32_t, int32_t> first_frame, std::pair<int32_t, int32_t> start_frame,
    std::pair<int32_t, int32_t> last_frame, std::pair<int32_t, int32_t> delta_frame, CARTA::CompressionType compression_type,
    float compression_quality, const std::vector<float>& tiles) {
    CARTA::StartAnimation start_animation;
    auto* mutable_first_frame = start_animation.mutable_first_frame();
    mutable_first_frame->set_channel(first_frame.first);
    mutable_first_frame->set_stokes(first_frame.second);

    auto* mutable_start_frame = start_animation.mutable_start_frame();
    mutable_start_frame->set_channel(start_frame.first);
    mutable_start_frame->set_stokes(start_frame.second);

    auto* mutable_last_frame = start_animation.mutable_last_frame();
    mutable_last_frame->set_channel(last_frame.first);
    mutable_last_frame->set_stokes(last_frame.second);

    auto* mutable_delta_frame = start_animation.mutable_delta_frame();
    mutable_delta_frame->set_channel(delta_frame.first);
    mutable_delta_frame->set_stokes(delta_frame.second);

    auto* mutable_required_tiles = start_animation.mutable_required_tiles();
    mutable_required_tiles->set_file_id(file_id);
    mutable_required_tiles->set_compression_type(compression_type);
    mutable_required_tiles->set_compression_quality(compression_quality);

    for (int i = 0; i < tiles.size(); ++i) {
        mutable_required_tiles->add_tiles(tiles[i]);
    }

    return start_animation;
}

CARTA::AnimationFlowControl GetAnimationFlowControl(int32_t file_id, std::pair<int32_t, int32_t> received_frame) {
    CARTA::AnimationFlowControl animation_flow_control;
    animation_flow_control.set_file_id(file_id);
    auto* mutable_received_frame = animation_flow_control.mutable_received_frame();
    mutable_received_frame->set_channel(received_frame.first);
    mutable_received_frame->set_stokes(received_frame.second);
    animation_flow_control.set_animation_id(1);

    const auto t_now = std::chrono::system_clock::now();
    animation_flow_control.set_timestamp(t_now.time_since_epoch().count());

    return animation_flow_control;
}

CARTA::StopAnimation GetStopAnimation(int32_t file_id, std::pair<int32_t, int32_t> end_frame) {
    CARTA::StopAnimation stop_animation;
    stop_animation.set_file_id(file_id);
    auto* mutable_end_frame = stop_animation.mutable_end_frame();
    mutable_end_frame->set_channel(end_frame.first);
    mutable_end_frame->set_stokes(end_frame.second);

    return stop_animation;
}

CARTA::SetSpatialRequirements_SpatialConfig GetSpatialConfig(std::string coordinate, int start, int end, int mip) {
    CARTA::SetSpatialRequirements_SpatialConfig spatial_config;
    spatial_config.set_coordinate(coordinate);
    spatial_config.set_start(start);
    spatial_config.set_end(end);
    spatial_config.set_mip(mip);
    return spatial_config;
}

CARTA::IntBounds GetIntBounds(int min, int max) {
    CARTA::IntBounds int_bounds;
    int_bounds.set_min(min);
    int_bounds.set_max(max);
    return int_bounds;
}

CARTA::FloatBounds GetFloatBounds(float min, float max) {
    CARTA::FloatBounds float_bounds;
    float_bounds.set_min(min);
    float_bounds.set_max(max);
    return float_bounds;
}

CARTA::MomentRequest GetMomentsRequest(int32_t file_id, int32_t region_id, CARTA::MomentAxis moments_axis, CARTA::MomentMask moment_mask,
    CARTA::IntBounds spectral_range, CARTA::FloatBounds pixel_range) {
    CARTA::MomentRequest moment_request;
    moment_request.set_file_id(file_id);
    moment_request.set_region_id(region_id);
    moment_request.set_axis(moments_axis);
    moment_request.set_mask(moment_mask);
    auto* mutable_spectral_range = moment_request.mutable_spectral_range();
    mutable_spectral_range->set_min(spectral_range.min());
    mutable_spectral_range->set_max(spectral_range.max());
    auto* mutable_pixel_range = moment_request.mutable_pixel_range();
    mutable_pixel_range->set_min(pixel_range.min());
    mutable_pixel_range->set_max(pixel_range.max());
    moment_request.add_moments(CARTA::Moment::MEAN_OF_THE_SPECTRUM);
    moment_request.add_moments(CARTA::Moment::INTEGRATED_OF_THE_SPECTRUM);
    moment_request.add_moments(CARTA::Moment::INTENSITY_WEIGHTED_COORD);
    moment_request.add_moments(CARTA::Moment::INTENSITY_WEIGHTED_DISPERSION_OF_THE_COORD);
    moment_request.add_moments(CARTA::Moment::MEDIAN_OF_THE_SPECTRUM);
    moment_request.add_moments(CARTA::Moment::MEDIAN_COORDINATE);
    moment_request.add_moments(CARTA::Moment::STD_ABOUT_THE_MEAN_OF_THE_SPECTRUM);
    moment_request.add_moments(CARTA::Moment::RMS_OF_THE_SPECTRUM);
    moment_request.add_moments(CARTA::Moment::ABS_MEAN_DEVIATION_OF_THE_SPECTRUM);
    moment_request.add_moments(CARTA::Moment::MAX_OF_THE_SPECTRUM);
    moment_request.add_moments(CARTA::Moment::COORD_OF_THE_MAX_OF_THE_SPECTRUM);
    moment_request.add_moments(CARTA::Moment::MIN_OF_THE_SPECTRUM);
    moment_request.add_moments(CARTA::Moment::COORD_OF_THE_MIN_OF_THE_SPECTRUM);
    return moment_request;
}

//--------------------------------------------------------

CARTA::EventType GetEventType(std::vector<char>& message) {
    carta::EventHeader head = *reinterpret_cast<const carta::EventHeader*>(message.data());
    return static_cast<CARTA::EventType>(head.type);
}

void LogRequestedEventType(const CARTA::EventType& event_type) {
    spdlog::debug("<== {}", EventType_Name(event_type));
}

void LogResponsiveEventType(const CARTA::EventType& event_type) {
    spdlog::debug("==> {}", EventType_Name(event_type));
}
