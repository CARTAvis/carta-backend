/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__UTIL_MESSAGE_H_
#define CARTA_BACKEND__UTIL_MESSAGE_H_

#include <carta-protobuf/animation.pb.h>
#include <carta-protobuf/close_file.pb.h>
#include <carta-protobuf/contour_image.pb.h>
#include <carta-protobuf/file_info.pb.h>
#include <carta-protobuf/file_list.pb.h>
#include <carta-protobuf/moment_request.pb.h>
#include <carta-protobuf/open_file.pb.h>
#include <carta-protobuf/region.pb.h>
#include <carta-protobuf/region_stats.pb.h>
#include <carta-protobuf/register_viewer.pb.h>
#include <carta-protobuf/resume_session.pb.h>
#include <carta-protobuf/set_cursor.pb.h>
#include <carta-protobuf/set_image_channels.pb.h>
#include <carta-protobuf/spectral_line_request.pb.h>
#include <carta-protobuf/spectral_profile.pb.h>
#include <carta-protobuf/stop_moment_calc.pb.h>
#include <carta-protobuf/tiles.pb.h>
#include <carta-protobuf/vector_overlay.pb.h>
#include <carta-protobuf/vector_overlay_tile.pb.h>

#include "Image.h"
#include "ImageStats/BasicStatsCalculator.h"
#include "ImageStats/Histogram.h"

namespace carta {
const uint16_t ICD_VERSION = 26;
struct EventHeader {
    uint16_t type;
    uint16_t icd_version;
    uint32_t request_id;
};
} // namespace carta

class Message {
    Message() {}
    ~Message() = default;

public:
    // Request messages
    static CARTA::RegisterViewer RegisterViewer(uint32_t session_id, std::string api_key, uint32_t client_feature_flags);
    static CARTA::CloseFile CloseFile(int32_t file_id);
    static CARTA::OpenFile OpenFile(
        std::string directory, std::string file, std::string hdu, int32_t file_id, CARTA::RenderMode render_mode, bool lel_expr = false);
    static CARTA::SetImageChannels SetImageChannels(
        int32_t file_id, int32_t channel, int32_t stokes, CARTA::CompressionType compression_type, float compression_quality);
    static CARTA::SetCursor SetCursor(int32_t file_id, float x, float y);
    static CARTA::SetSpatialRequirements SetSpatialRequirements(int32_t file_id, int32_t region_id);
    static CARTA::SetStatsRequirements SetStatsRequirements(int32_t file_id, int32_t region_id);
    static CARTA::SetHistogramRequirements SetHistogramRequirements(
        int32_t file_id, int32_t region_id, int32_t channel = CURRENT_Z, int32_t num_bins = AUTO_BIN_SIZE);
    static CARTA::AddRequiredTiles AddRequiredTiles(
        int32_t file_id, CARTA::CompressionType compression_type, float compression_quality, const std::vector<float>& tiles);
    static CARTA::Point Point(int x, int y);
    static CARTA::SetRegion SetRegion(
        int32_t file_id, int32_t region_id, CARTA::RegionType region_type, std::vector<CARTA::Point> control_points, float rotation);
    static CARTA::SetStatsRequirements SetStatsRequirements(int32_t file_id, int32_t region_id, std::string coordinate);
    static CARTA::SetSpectralRequirements SetSpectralRequirements(int32_t file_id, int32_t region_id, std::string coordinate);
    static CARTA::StartAnimation StartAnimation(int32_t file_id, std::pair<int32_t, int32_t> first_frame,
        std::pair<int32_t, int32_t> start_frame, std::pair<int32_t, int32_t> last_frame, std::pair<int32_t, int32_t> delta_frame,
        CARTA::CompressionType compression_type, float compression_quality, const std::vector<float>& tiles, int32_t frame_rate = 5);
    static CARTA::AnimationFlowControl AnimationFlowControl(int32_t file_id, std::pair<int32_t, int32_t> received_frame);
    static CARTA::StopAnimation StopAnimation(int32_t file_id, std::pair<int32_t, int32_t> end_frame);
    static CARTA::SetSpatialRequirements_SpatialConfig SpatialConfig(std::string coordinate, int start = 0, int end = 0, int mip = 0);
    static CARTA::IntBounds IntBounds(int min, int max);
    static CARTA::FloatBounds FloatBounds(float min, float max);
    static CARTA::MomentRequest MomentsRequest(int32_t file_id, int32_t region_id, CARTA::MomentAxis moments_axis,
        CARTA::MomentMask moment_mask, CARTA::IntBounds spectral_range, CARTA::FloatBounds pixel_range);
    static CARTA::ImageProperties ImageProperties(std::string directory, std::string file, std::string hdu, int32_t file_id,
        CARTA::RenderMode render_mode, int32_t channel, int32_t stokes);
    static CARTA::ResumeSession ResumeSession(std::vector<CARTA::ImageProperties> images);
    static CARTA::FileListRequest FileListRequest(const std::string& directory);
    static CARTA::FileInfoRequest FileInfoRequest(const std::string& directory, const std::string& file, const std::string& hdu = "");
    static CARTA::SetContourParameters SetContourParameters(int file_id, int ref_file_id, int x_min, int x_max, int y_min, int y_max,
        const std::vector<double>& levels, CARTA::SmoothingMode smoothing_mode, int smoothing_factor, int decimation_factor,
        int compression_level, int contour_chunk_size);
    static CARTA::SetVectorOverlayParameters SetVectorOverlayParameters(int file_id, int mip, bool fractional, bool debiasing,
        double q_error, double u_error, double threshold, int stokes_intensity, int stokes_angle,
        const CARTA::CompressionType& compression_type, float compression_quality);

    // Response messages
    static CARTA::SpectralProfileData SpectralProfileData(int32_t file_id, int32_t region_id, int32_t stokes, float progress,
        std::string& coordinate, std::vector<CARTA::StatsType>& required_stats,
        std::map<CARTA::StatsType, std::vector<double>>& spectral_data);

    // Decode messages
    static CARTA::EventType EventType(std::vector<char>& message);

    template <typename T>
    static T DecodeMessage(std::vector<char>& message) {
        T decoded_message;
        char* event_buf = message.data() + sizeof(carta::EventHeader);
        int event_length = message.size() - sizeof(carta::EventHeader);
        decoded_message.ParseFromArray(event_buf, event_length);
        return decoded_message;
    }
};

void FillHistogram(CARTA::Histogram* histogram, int num_bins, double bin_width, double first_bin_center, const std::vector<int>& bins,
    double mean, double std_dev);
void FillHistogram(CARTA::Histogram* histogram, const carta::BasicStats<float>& stats, const carta::Histogram& hist);
void FillStatistics(CARTA::RegionStatsData& stats_data, const std::vector<CARTA::StatsType>& required_stats,
    std::map<CARTA::StatsType, double>& stats_value_map);

#endif // CARTA_BACKEND__UTIL_MESSAGE_H_
