/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Message.h"
#include "Cache/RequirementsCache.h"
#include "DataStream/Compression.h"
#include "Util/Nan.h"

#include <chrono>

CARTA::CloseFile Message::CloseFile(int32_t file_id) {
    CARTA::CloseFile close_file;
    close_file.set_file_id(file_id);
    return close_file;
}

CARTA::OpenFile Message::OpenFile(std::string directory, std::string file, bool lel_expr, std::string hdu, int32_t file_id,
    bool support_aips_beam, CARTA::RenderMode render_mode) {
    CARTA::OpenFile open_file;
    open_file.set_directory(directory);
    open_file.set_file(file);
    open_file.set_hdu(hdu);
    open_file.set_file_id(file_id);
    open_file.set_render_mode(render_mode);
    open_file.set_lel_expr(lel_expr);
    open_file.set_support_aips_beam(support_aips_beam);
    return open_file;
}

CARTA::SetImageChannels Message::SetImageChannels(
    int32_t file_id, int32_t channel, int32_t stokes, CARTA::CompressionType compression_type, float compression_quality) {
    CARTA::SetImageChannels set_image_channels;
    set_image_channels.set_file_id(file_id);
    set_image_channels.set_channel(channel);
    set_image_channels.set_stokes(stokes);
    if (compression_quality > -1) {
        CARTA::AddRequiredTiles* required_tiles = set_image_channels.mutable_required_tiles();
        required_tiles->set_file_id(file_id);
        required_tiles->set_compression_type(compression_type);
        required_tiles->set_compression_quality(compression_quality);
        required_tiles->add_tiles(0);
    }
    return set_image_channels;
}

CARTA::AddRequiredTiles Message::AddRequiredTiles(
    int32_t file_id, CARTA::CompressionType compression_type, float compression_quality, const std::vector<int32_t>& tiles) {
    CARTA::AddRequiredTiles add_required_tiles;
    add_required_tiles.set_file_id(file_id);
    add_required_tiles.set_compression_type(compression_type);
    add_required_tiles.set_compression_quality(compression_quality);
    for (int i = 0; i < tiles.size(); ++i) {
        add_required_tiles.add_tiles(tiles[i]);
    }
    return add_required_tiles;
}

CARTA::Point Message::Point(float x, float y) {
    CARTA::Point point;
    point.set_x(x);
    point.set_y(y);
    return point;
}

CARTA::Point Message::Point(const casacore::Vector<casacore::Double>& input, int x_index, int y_index) {
    return Message::Point(input(x_index), input(y_index));
}

CARTA::Point Message::Point(const std::vector<casacore::Quantity>& input, int x_index, int y_index) {
    return Message::Point(input[x_index].getValue(), input[y_index].getValue());
}

CARTA::Point Message::Point(const std::vector<double>& input, int x_index, int y_index) {
    return Message::Point(input[x_index], input[y_index]);
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

CARTA::ImageBounds Message::ImageBounds(int32_t x_min, int32_t x_max, int32_t y_min, int32_t y_max) {
    CARTA::ImageBounds message;
    message.set_x_min(x_min);
    message.set_x_max(x_max);
    message.set_y_min(y_min);
    message.set_y_max(y_max);
    return message;
}

CARTA::SetRegion Message::SetRegion(int32_t file_id, int32_t region_id, const CARTA::RegionInfo& region_info) {
    CARTA::SetRegion message;
    message.set_file_id(file_id);
    message.set_region_id(region_id);
    *message.mutable_region_info() = region_info;
    return message;
}

CARTA::ConcatStokesFiles Message::ConcatStokesFiles(
    int32_t file_id, const google::protobuf::RepeatedPtrField<CARTA::StokesFile>& stokes_files) {
    CARTA::ConcatStokesFiles message;
    message.set_file_id(file_id);
    *message.mutable_stokes_files() = stokes_files;
    return message;
}

CARTA::DoublePoint Message::DoublePoint(double x, double y) {
    CARTA::DoublePoint message;
    message.set_x(x);
    message.set_y(y);
    return message;
}

CARTA::GaussianComponent Message::GaussianComponent(
    const CARTA::DoublePoint& center, double amp, const CARTA::DoublePoint& fwhm, double pa) {
    CARTA::GaussianComponent message;
    *message.mutable_center() = center;
    message.set_amp(amp);
    *message.mutable_fwhm() = fwhm;
    message.set_pa(pa);
    return message;
}

CARTA::ScriptingRequest Message::ScriptingRequest(uint32_t scripting_request_id, const std::string& target, const std::string& action,
    const std::string& parameters, bool async, const std::string& return_path) {
    CARTA::ScriptingRequest message;
    message.set_scripting_request_id(scripting_request_id);
    message.set_target(target);
    message.set_action(action);
    message.set_parameters(parameters);
    message.set_async(async);
    message.set_return_path(return_path);
    return message;
}

carta::EventHeader Message::GetEventHeader(std::string_view message) {
    return *reinterpret_cast<const carta::EventHeader*>(message.data());
}

/**
 * @note This function creates a binary buffer containing a CARTA::EventHeader followed by the serialized protobuf message.
 * The header includes the event type, protocol version, and event/request ID.
 */
std::vector<char> Message::EncodeMessage(CARTA::EventType event_type, uint32_t event_id, const google::protobuf::MessageLite& message) {
    size_t message_length = message.ByteSizeLong();
    size_t required_size = sizeof(carta::EventHeader) + message_length;

    std::vector<char> msg(required_size);
    carta::EventHeader* header = reinterpret_cast<carta::EventHeader*>(msg.data());
    header->type = event_type;
    header->icd_version = carta::ICD_VERSION;
    header->request_id = event_id;

    message.SerializeToArray(msg.data() + sizeof(carta::EventHeader), message_length);

    return msg;
}

CARTA::SpectralProfileData Message::SpectralProfileData(int32_t stokes, float progress, int32_t file_id, int32_t region_id) {
    CARTA::SpectralProfileData profile_message;
    profile_message.set_file_id(file_id);
    profile_message.set_region_id(region_id);
    profile_message.set_stokes(stokes);
    profile_message.set_progress(progress);

    return profile_message;
}

CARTA::SpectralProfile* Message::AddProfile(CARTA::SpectralProfileData& profile_data, std::string& coordinate,
    CARTA::StatsType& stats_type, std::vector<double>& values) {
    // one SpectralProfile per stats type
    auto* profile = profile_data.add_profiles();
    profile->set_coordinate(coordinate);
    profile->set_stats_type(stats_type);
    profile->set_raw_values_fp64(values.data(), values.size() * sizeof(double));

    return profile;
}

CARTA::SpectralProfile* Message::AddProfile(
    CARTA::SpectralProfileData& profile_data, std::string& coordinate, CARTA::StatsType& stats_type, std::vector<float>& values) {
    auto* profile = profile_data.add_profiles();
    profile->set_coordinate(coordinate);
    profile->set_stats_type(stats_type);
    profile->set_raw_values_fp32(values.data(), values.size() * sizeof(float));

    return profile;
}

CARTA::SpatialProfileData Message::SpatialProfileData(
    int32_t x, int32_t y, int32_t channel, int32_t stokes, float value, int32_t file_id, int32_t region_id) {
    CARTA::SpatialProfileData profile_message;
    profile_message.set_file_id(file_id);
    profile_message.set_region_id(region_id);
    profile_message.set_x(x);
    profile_message.set_y(y);
    profile_message.set_channel(channel);
    profile_message.set_stokes(stokes);
    profile_message.set_value(value);
    return profile_message;
}

CARTA::SpatialProfile* Message::AddProfile(CARTA::SpatialProfileData& profile_message, int32_t start, int32_t end,
    std::vector<float>& values, std::string coordinate, int32_t mip) {
    auto profile = profile_message.add_profiles();
    profile->set_start(start);
    profile->set_end(end);
    profile->set_raw_values_fp32(values.data(), values.size() * sizeof(float));
    profile->set_coordinate(coordinate);
    profile->set_mip(mip);

    return profile;
}

CARTA::LineProfileAxis* Message::AddLineProfileAxis(
    CARTA::SpatialProfile* spatial_profile, CARTA::ProfileAxisType axis_type, float crpix, float crval, float cdelt, std::string unit) {
    auto axis = spatial_profile->mutable_line_axis();
    axis->set_axis_type(axis_type);
    axis->set_crpix(crpix);
    axis->set_crval(crval);
    axis->set_cdelt(cdelt);
    axis->set_unit(unit);

    return axis;
}

CARTA::RasterTileSync Message::RasterTileSync(
    int32_t file_id, int32_t channel, int32_t stokes, int32_t sync_id, int32_t animation_id, int32_t tile_count, bool end_sync) {
    CARTA::RasterTileSync message;
    message.set_file_id(file_id);
    message.set_channel(channel);
    message.set_stokes(stokes);
    message.set_sync_id(sync_id);
    message.set_animation_id(animation_id);
    message.set_tile_count(tile_count);
    message.set_end_sync(end_sync);
    return message;
}

CARTA::SetRegionAck Message::SetRegionAck(int32_t region_id, bool success, std::string err_message) {
    CARTA::SetRegionAck message;
    message.set_region_id(region_id);
    message.set_success(success);
    message.set_message(err_message);
    return message;
}

CARTA::RegisterViewerAck Message::RegisterViewerAck(
    uint32_t session_id, bool success, const std::string& status, const CARTA::SessionType& type) {
    CARTA::RegisterViewerAck message;
    message.set_session_id(session_id);
    message.set_success(success);
    message.set_message(status);
    message.set_session_type(type);
    return message;
}

CARTA::MomentProgress Message::MomentProgress(int32_t file_id, float progress) {
    CARTA::MomentProgress message;
    message.set_file_id(file_id);
    message.set_progress(progress);
    return message;
}

CARTA::PvProgress Message::PvProgress(int32_t file_id, float progress, int32_t preview_id) {
    CARTA::PvProgress message;
    message.set_file_id(file_id);
    message.set_preview_id(preview_id);
    message.set_progress(progress);
    return message;
}

CARTA::FittingProgress Message::FittingProgress(int32_t file_id, float progress) {
    CARTA::FittingProgress message;
    message.set_file_id(file_id);
    message.set_progress(progress);
    return message;
}

CARTA::RegionHistogramData Message::RegionHistogramData(
    int32_t file_id, int32_t region_id, int32_t channel, int32_t stokes, float progress, const carta::HistogramConfig& hist_config) {
    CARTA::RegionHistogramData message;
    message.set_file_id(file_id);
    message.set_region_id(region_id);
    message.set_channel(channel);
    message.set_stokes(stokes);
    message.set_progress(progress);
    auto* config = message.mutable_config();
    config->set_fixed_num_bins(hist_config.fixed_num_bins);
    config->set_num_bins(hist_config.num_bins);
    config->set_fixed_bounds(hist_config.fixed_bounds);
    auto* bounds = config->mutable_bounds();
    bounds->set_min(hist_config.bounds.min);
    bounds->set_max(hist_config.bounds.max);
    return message;
}

CARTA::ContourImageData Message::ContourImageData(
    int32_t file_id, uint32_t reference_file_id, int32_t channel, int32_t stokes, double progress) {
    CARTA::ContourImageData message;
    message.set_file_id(file_id);
    message.set_reference_file_id(reference_file_id);
    message.set_channel(channel);
    message.set_stokes(stokes);
    message.set_progress(progress);
    return message;
}

CARTA::VectorOverlayTileData Message::VectorOverlayTileData(int32_t file_id, int32_t channel, int32_t stokes_intensity,
    int32_t stokes_angle, const CARTA::CompressionType& compression_type, float compression_quality) {
    CARTA::VectorOverlayTileData message;
    message.set_file_id(file_id);
    message.set_channel(channel);
    message.set_stokes_intensity(stokes_intensity);
    message.set_stokes_angle(stokes_angle);
    message.set_compression_type(compression_type);
    message.set_compression_quality(compression_quality);
    return message;
}

CARTA::ErrorData Message::ErrorData(const std::string& message, std::vector<std::string> tags, CARTA::ErrorSeverity severity) {
    CARTA::ErrorData error_data;
    error_data.set_message(message);
    error_data.set_severity(severity);
    *error_data.mutable_tags() = {tags.begin(), tags.end()};
    return error_data;
}

CARTA::FileInfo Message::FileInfo(const std::string& name, CARTA::FileType type, int64_t size, const std::string& hdu) {
    CARTA::FileInfo message;
    message.set_name(name);
    message.set_type(type);
    message.set_size(size);
    message.add_hdu_list(hdu);
    return message;
}

CARTA::RasterTileData Message::RasterTileData(int32_t file_id, int32_t sync_id, int32_t animation_id) {
    CARTA::RasterTileData message;
    message.set_file_id(file_id);
    message.set_sync_id(sync_id);
    message.set_animation_id(animation_id);
    return message;
}

CARTA::StartAnimationAck Message::StartAnimationAck(bool success, int32_t animation_id, const std::string& message) {
    CARTA::StartAnimationAck start_animation_ack;
    start_animation_ack.set_success(success);
    start_animation_ack.set_animation_id(animation_id);
    start_animation_ack.set_message(message);
    return start_animation_ack;
}

CARTA::ImportRegionAck Message::ImportRegionAck(bool success, const std::string& message) {
    CARTA::ImportRegionAck import_region_ack;
    import_region_ack.set_success(success);
    import_region_ack.set_message(message);
    return import_region_ack;
}

CARTA::RegionStatsData Message::RegionStatsData(int32_t file_id, int32_t region_id, int32_t channel, int32_t stokes) {
    CARTA::RegionStatsData message;
    message.set_file_id(file_id);
    message.set_region_id(region_id);
    message.set_channel(channel);
    message.set_stokes(stokes);
    return message;
}

CARTA::Beam Message::Beam(int32_t channel, int32_t stokes, float major_axis, float minor_axis, float pa) {
    CARTA::Beam message;
    message.set_channel(channel);
    message.set_stokes(stokes);
    message.set_major_axis(major_axis);
    message.set_minor_axis(minor_axis);
    message.set_pa(pa);
    return message;
}

CARTA::ListProgress Message::ListProgress(
    const CARTA::FileListType& file_list_type, int32_t total_count, int32_t checked_count, float percentage) {
    CARTA::ListProgress message;
    message.set_file_list_type(file_list_type);
    message.set_total_count(total_count);
    message.set_checked_count(checked_count);
    message.set_percentage(percentage);
    return message;
}

void FillHistogram(CARTA::Histogram* histogram, int32_t num_bins, double bin_width, double first_bin_center,
    const std::vector<int32_t>& bins, double mean, double std_dev) {
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
                value = DOUBLE_NAN;
            }
        }

        // add StatisticsValue to message
        auto* stats_value = stats_data.add_statistics();
        stats_value->set_stats_type(carta_stats_type);
        stats_value->set_value(value);
    }
}

CARTA::HeaderEntry* Message::AddComputedEntry(
    CARTA::FileInfoExtended& response, std::string name, const std::string& value, CARTA::EntryType type, double numeric_value) {
    auto entry = response.add_computed_entries();
    entry->set_name(name);
    entry->set_value(value);
    entry->set_entry_type(type);
    entry->set_numeric_value(numeric_value);
    return entry;
}

CARTA::HeaderEntry* Message::AddHeaderEntry(
    CARTA::FileInfoExtended& response, std::string name, const std::string& value, CARTA::EntryType type, double numeric_value) {
    auto entry = response.add_header_entries();
    entry->set_name(name);
    entry->set_value(value);
    entry->set_entry_type(type);
    entry->set_numeric_value(numeric_value);
    return entry;
}

CARTA::FileInfoExtended Message::SetDimensions(
    CARTA::FileInfoExtended& response, int32_t dimensions, int32_t width, int32_t height, int32_t depth, int32_t stokes) {
    response.set_dimensions(dimensions);
    response.set_width(width);
    response.set_height(height);
    response.set_depth(depth);
    response.set_stokes(stokes);
    return response;
}

CARTA::AxesNumbers* Message::AddAxesNumbers(
    CARTA::FileInfoExtended& response, int32_t spatial_x, int32_t spatial_y, int32_t spectral, int32_t stokes, int32_t depth) {
    auto* axes_numbers_info = response.mutable_axes_numbers();
    axes_numbers_info->set_spatial_x(spatial_x);
    axes_numbers_info->set_spatial_y(spatial_y);
    axes_numbers_info->set_spectral(spectral);
    axes_numbers_info->set_stokes(stokes);
    axes_numbers_info->set_depth(depth);
    return axes_numbers_info;
}
