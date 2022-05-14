/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Message.h"
#include "DataStream/Compression.h"

#include <chrono>

CARTA::RegisterViewer Message::RegisterViewer(uint32_t session_id, std::string api_key, uint32_t client_feature_flags) {
    CARTA::RegisterViewer register_viewer;
    register_viewer.set_session_id(session_id);
    register_viewer.set_api_key(api_key);
    register_viewer.set_client_feature_flags(client_feature_flags);
    return register_viewer;
}

CARTA::CloseFile Message::CloseFile(int32_t file_id) {
    CARTA::CloseFile close_file;
    close_file.set_file_id(file_id);
    return close_file;
}

CARTA::OpenFile Message::OpenFile(
    std::string directory, std::string file, std::string hdu, int32_t file_id, CARTA::RenderMode render_mode, bool lel_expr) {
    CARTA::OpenFile open_file;
    open_file.set_directory(directory);
    open_file.set_file(file);
    open_file.set_hdu(hdu);
    open_file.set_file_id(file_id);
    open_file.set_render_mode(render_mode);
    open_file.set_lel_expr(lel_expr);
    return open_file;
}

CARTA::SetImageChannels Message::SetImageChannels(
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

CARTA::SetCursor Message::SetCursor(int32_t file_id, float x, float y) {
    CARTA::SetCursor set_cursor;
    set_cursor.set_file_id(file_id);
    auto* point = set_cursor.mutable_point();
    point->set_x(x);
    point->set_y(y);
    return set_cursor;
}

CARTA::SetSpatialRequirements Message::SetSpatialRequirements(int32_t file_id, int32_t region_id) {
    CARTA::SetSpatialRequirements set_spatial_requirements;
    set_spatial_requirements.set_file_id(file_id);
    set_spatial_requirements.set_region_id(region_id);
    auto* spatial_requirement_x = set_spatial_requirements.add_spatial_profiles();
    spatial_requirement_x->set_coordinate("x");
    auto* spatial_requirement_y = set_spatial_requirements.add_spatial_profiles();
    spatial_requirement_y->set_coordinate("y");
    return set_spatial_requirements;
}

CARTA::SetStatsRequirements Message::SetStatsRequirements(int32_t file_id, int32_t region_id) {
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

CARTA::SetHistogramRequirements Message::SetHistogramRequirements(int32_t file_id, int32_t region_id, int32_t channel, int32_t num_bins) {
    CARTA::SetHistogramRequirements set_histogram_requirements;
    set_histogram_requirements.set_file_id(file_id);
    set_histogram_requirements.set_region_id(region_id);
    auto* histograms = set_histogram_requirements.add_histograms();
    histograms->set_channel(channel);
    histograms->set_num_bins(num_bins);
    return set_histogram_requirements;
}

CARTA::AddRequiredTiles Message::AddRequiredTiles(
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

CARTA::Point Message::Point(int x, int y) {
    CARTA::Point point;
    point.set_x(x);
    point.set_y(y);
    return point;
}

CARTA::SetRegion Message::SetRegion(
    int32_t file_id, int32_t region_id, CARTA::RegionType region_type, std::vector<CARTA::Point> control_points, float rotation) {
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

CARTA::SetStatsRequirements Message::SetStatsRequirements(int32_t file_id, int32_t region_id, std::string coordinate) {
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

CARTA::SetSpectralRequirements Message::SetSpectralRequirements(int32_t file_id, int32_t region_id, std::string coordinate) {
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

CARTA::StartAnimation Message::StartAnimation(int32_t file_id, std::pair<int32_t, int32_t> first_frame,
    std::pair<int32_t, int32_t> start_frame, std::pair<int32_t, int32_t> last_frame, std::pair<int32_t, int32_t> delta_frame,
    CARTA::CompressionType compression_type, float compression_quality, const std::vector<float>& tiles, int32_t frame_rate) {
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

    start_animation.set_frame_rate(frame_rate);

    return start_animation;
}

CARTA::AnimationFlowControl Message::AnimationFlowControl(int32_t file_id, std::pair<int32_t, int32_t> received_frame) {
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

CARTA::StopAnimation Message::StopAnimation(int32_t file_id, std::pair<int32_t, int32_t> end_frame) {
    CARTA::StopAnimation stop_animation;
    stop_animation.set_file_id(file_id);
    auto* mutable_end_frame = stop_animation.mutable_end_frame();
    mutable_end_frame->set_channel(end_frame.first);
    mutable_end_frame->set_stokes(end_frame.second);

    return stop_animation;
}

CARTA::SetSpatialRequirements_SpatialConfig Message::SpatialConfig(std::string coordinate, int start, int end, int mip, int width) {
    CARTA::SetSpatialRequirements_SpatialConfig spatial_config;
    spatial_config.set_coordinate(coordinate);
    spatial_config.set_start(start);
    spatial_config.set_end(end);
    spatial_config.set_mip(mip);
    spatial_config.set_width(width);
    return spatial_config;
}

CARTA::IntBounds Message::IntBounds(int min, int max) {
    CARTA::IntBounds int_bounds;
    int_bounds.set_min(min);
    int_bounds.set_max(max);
    return int_bounds;
}

CARTA::FloatBounds Message::FloatBounds(float min, float max) {
    CARTA::FloatBounds float_bounds;
    float_bounds.set_min(min);
    float_bounds.set_max(max);
    return float_bounds;
}

CARTA::MomentRequest Message::MomentsRequest(int32_t file_id, int32_t region_id, CARTA::MomentAxis moments_axis,
    CARTA::MomentMask moment_mask, CARTA::IntBounds spectral_range, CARTA::FloatBounds pixel_range) {
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

CARTA::ImageProperties Message::ImageProperties(std::string directory, std::string file, std::string hdu, int32_t file_id,
    CARTA::RenderMode render_mode, int32_t channel, int32_t stokes) {
    CARTA::ImageProperties image_properties;
    image_properties.set_directory(directory);
    image_properties.set_file(file);
    image_properties.set_hdu(hdu);
    image_properties.set_file_id(file_id);
    image_properties.set_render_mode(render_mode);
    image_properties.set_channel(channel);
    image_properties.set_stokes(stokes);
    return image_properties;
}

CARTA::ResumeSession Message::ResumeSession(std::vector<CARTA::ImageProperties> images) {
    CARTA::ResumeSession resume_session;
    for (auto image : images) {
        auto* tmp_image = resume_session.add_images();
        tmp_image->set_directory(image.directory());
        tmp_image->set_file(image.file());
        tmp_image->set_hdu(image.hdu());
        tmp_image->set_file_id(image.file_id());
        tmp_image->set_render_mode(image.render_mode());
        tmp_image->set_channel(image.channel());
        tmp_image->set_stokes(image.stokes());
    }
    return resume_session;
}

CARTA::SetSpectralRequirements_SpectralConfig Message::SpectralConfig(const std::string& coordinate) {
    CARTA::SetSpectralRequirements_SpectralConfig spectral_config;
    spectral_config.set_coordinate(coordinate);
    spectral_config.add_stats_types(CARTA::StatsType::Mean);
    return spectral_config;
}

CARTA::FileListRequest Message::FileListRequest(const std::string& directory) {
    CARTA::FileListRequest file_list_request;
    file_list_request.set_directory(directory);
    return file_list_request;
}

CARTA::FileInfoRequest Message::FileInfoRequest(const std::string& directory, const std::string& file, const std::string& hdu) {
    CARTA::FileInfoRequest file_info_request;
    file_info_request.set_directory(directory);
    file_info_request.set_file(file);
    file_info_request.set_hdu(hdu);
    return file_info_request;
}

CARTA::SetContourParameters Message::SetContourParameters(int file_id, int ref_file_id, int x_min, int x_max, int y_min, int y_max,
    const std::vector<double>& levels, CARTA::SmoothingMode smoothing_mode, int smoothing_factor, int decimation_factor,
    int compression_level, int contour_chunk_size) {
    CARTA::SetContourParameters message;
    message.set_file_id(file_id);
    message.set_reference_file_id(ref_file_id);
    auto* image_bounds = message.mutable_image_bounds();
    image_bounds->set_x_min(x_min);
    image_bounds->set_x_max(x_max);
    image_bounds->set_y_min(y_min);
    image_bounds->set_y_max(y_max);
    for (auto level : levels) {
        message.add_levels(level);
    }
    message.set_smoothing_mode(smoothing_mode);
    message.set_smoothing_factor(smoothing_factor);
    message.set_decimation_factor(decimation_factor);
    message.set_compression_level(compression_level);
    message.set_contour_chunk_size(contour_chunk_size);
    return message;
}

CARTA::SetVectorOverlayParameters Message::SetVectorOverlayParameters(int file_id, int mip, bool fractional, double threshold,
    bool debiasing, double q_error, double u_error, int stokes_intensity, int stokes_angle, const CARTA::CompressionType& compression_type,
    float compression_quality) {
    CARTA::SetVectorOverlayParameters message;
    message.set_file_id(file_id);
    message.set_smoothing_factor(mip);
    message.set_fractional(fractional);
    message.set_threshold(threshold);
    message.set_debiasing(debiasing);
    message.set_q_error(q_error);
    message.set_u_error(u_error);
    message.set_stokes_intensity(stokes_intensity);
    message.set_stokes_angle(stokes_angle);
    message.set_compression_type(compression_type);
    message.set_compression_quality(compression_quality);
    return message;
}

CARTA::ImageBounds Message::ImageBounds(int x_min, int x_max, int y_min, int y_max) {
    CARTA::ImageBounds message;
    message.set_x_min(x_min);
    message.set_x_max(x_max);
    message.set_y_min(y_min);
    message.set_y_max(y_max);
    return message;
}

CARTA::EventType Message::EventType(std::vector<char>& message) {
    carta::EventHeader head = *reinterpret_cast<const carta::EventHeader*>(message.data());
    return static_cast<CARTA::EventType>(head.type);
}

CARTA::SpectralProfileData Message::SpectralProfileData(int32_t file_id, int32_t region_id, int32_t stokes, float progress,
    std::string& coordinate, std::vector<CARTA::StatsType>& required_stats,
    std::map<CARTA::StatsType, std::vector<double>>& spectral_data) {
    CARTA::SpectralProfileData profile_message;
    profile_message.set_file_id(file_id);
    profile_message.set_region_id(region_id);
    profile_message.set_stokes(stokes);
    profile_message.set_progress(progress);

    for (auto stats_type : required_stats) {
        // one SpectralProfile per stats type
        auto new_profile = profile_message.add_profiles();
        new_profile->set_coordinate(coordinate);
        new_profile->set_stats_type(stats_type);

        if (spectral_data.find(stats_type) == spectral_data.end()) { // stat not provided
            double nan_value = std::nan("");
            new_profile->set_raw_values_fp64(&nan_value, sizeof(double));
        } else {
            new_profile->set_raw_values_fp64(spectral_data[stats_type].data(), spectral_data[stats_type].size() * sizeof(double));
        }
    }
    return profile_message;
}

CARTA::SpatialProfileData Message::SpatialProfileData(int32_t file_id, int32_t region_id, int32_t x, int32_t y, int32_t channel,
    int32_t stokes, float value, int32_t start, int32_t end, std::vector<float>& profile, std::string& coordinate, int32_t mip,
    CARTA::ProfileAxisType axis_type, float crpix, float crval, float cdelt, std::string& unit) {
    CARTA::SpatialProfileData profile_message;
    profile_message.set_file_id(file_id);
    profile_message.set_region_id(region_id);
    profile_message.set_x(x);
    profile_message.set_y(y);
    profile_message.set_channel(channel);
    profile_message.set_stokes(stokes);
    profile_message.set_value(value);
    auto spatial_profile = profile_message.add_profiles();
    spatial_profile->set_start(start);
    spatial_profile->set_end(end);
    spatial_profile->set_raw_values_fp32(profile.data(), profile.size() * sizeof(float));
    spatial_profile->set_coordinate(coordinate);
    spatial_profile->set_mip(mip);
    auto profile_axis = spatial_profile->mutable_line_axis();
    profile_axis->set_axis_type(axis_type);
    profile_axis->set_crpix(crpix);
    profile_axis->set_crval(crval);
    profile_axis->set_cdelt(cdelt);
    profile_axis->set_unit(unit);
    return profile_message;
}

void FillHistogram(CARTA::Histogram* histogram, int num_bins, double bin_width, double first_bin_center, const std::vector<int>& bins,
    double mean, double std_dev) {
    if (histogram) {
        histogram->set_num_bins(num_bins);
        histogram->set_bin_width(bin_width);
        histogram->set_first_bin_center(first_bin_center);
        *histogram->mutable_bins() = {bins.begin(), bins.end()};
        histogram->set_mean(mean);
        histogram->set_std_dev(std_dev);
    }
}

void FillHistogram(CARTA::Histogram* histogram, const carta::BasicStats<float>& stats, const carta::Histogram& hist) {
    FillHistogram(histogram, hist.GetNbins(), hist.GetBinWidth(), hist.GetBinCenter(), hist.GetHistogramBins(), stats.mean, stats.stdDev);
}

void FillStatistics(CARTA::RegionStatsData& stats_data, const std::vector<CARTA::StatsType>& required_stats,
    std::map<CARTA::StatsType, double>& stats_value_map) {
    // inserts values from map into message StatisticsValue field; needed by Frame and RegionDataHandler
    for (auto type : required_stats) {
        double value(0.0); // default
        auto carta_stats_type = static_cast<CARTA::StatsType>(type);
        if (stats_value_map.find(carta_stats_type) != stats_value_map.end()) { // stat found
            value = stats_value_map[carta_stats_type];
        } else { // stat not provided
            if (carta_stats_type != CARTA::StatsType::NumPixels) {
                value = std::nan("");
            }
        }

        // add StatisticsValue to message
        auto stats_value = stats_data.add_statistics();
        stats_value->set_stats_type(carta_stats_type);
        stats_value->set_value(value);
    }
}
