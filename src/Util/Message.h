/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_UTIL_MESSAGE_H_
#define CARTA_SRC_UTIL_MESSAGE_H_

#include <carta-protobuf/animation.pb.h>
#include <carta-protobuf/channel_map.pb.h>
#include <carta-protobuf/close_file.pb.h>
#include <carta-protobuf/contour_image.pb.h>
#include <carta-protobuf/defs.pb.h>
#include <carta-protobuf/error.pb.h>
#include <carta-protobuf/export_region.pb.h>
#include <carta-protobuf/file_info.pb.h>
#include <carta-protobuf/file_list.pb.h>
#include <carta-protobuf/fitting_request.pb.h>
#include <carta-protobuf/import_region.pb.h>
#include <carta-protobuf/moment_request.pb.h>
#include <carta-protobuf/open_file.pb.h>
#include <carta-protobuf/pv_request.pb.h>
#include <carta-protobuf/raster_tile.pb.h>
#include <carta-protobuf/region.pb.h>
#include <carta-protobuf/region_histogram.pb.h>
#include <carta-protobuf/region_requirements.pb.h>
#include <carta-protobuf/region_stats.pb.h>
#include <carta-protobuf/register_viewer.pb.h>
#include <carta-protobuf/remote_file_request.pb.h>
#include <carta-protobuf/resume_session.pb.h>
#include <carta-protobuf/save_file.pb.h>
#include <carta-protobuf/scripting.pb.h>
#include <carta-protobuf/set_cursor.pb.h>
#include <carta-protobuf/set_image_channels.pb.h>
#include <carta-protobuf/spatial_profile.pb.h>
#include <carta-protobuf/spectral_profile.pb.h>
#include <carta-protobuf/stop_moment_calc.pb.h>
#include <carta-protobuf/stop_pv_calc.pb.h>
#include <carta-protobuf/tiles.pb.h>
#include <carta-protobuf/vector_overlay.pb.h>
#include <carta-protobuf/vector_overlay_tile.pb.h>

#include <casacore/casa/Quanta/Quantum.h>

#include "Image.h"
#include "ImageStats/BasicStatsCalculator.h"
#include "ImageStats/Histogram.h"

namespace carta {
const uint16_t ICD_VERSION = 30;

struct EventHeader {
    uint16_t type;
    uint16_t icd_version;
    uint32_t request_id;

    CARTA::EventType GetType() const {
        return static_cast<CARTA::EventType>(type);
    }
};
struct HistogramConfig;
} // namespace carta

class Message {
    Message() {}
    ~Message() = default;

public:
    // Request messages
    static CARTA::CloseFile CloseFile(int32_t file_id);
    static CARTA::OpenFile OpenFile(std::string directory, std::string file, bool lel_expr, std::string hdu, int32_t file_id,
        bool support_aips_beam, CARTA::RenderMode render_mode = CARTA::RenderMode::RASTER);
    static CARTA::SetImageChannels SetImageChannels(int32_t file_id, int32_t channel, int32_t stokes,
        CARTA::CompressionType compression_type = CARTA::CompressionType::NONE, float compression_quality = -1);
    static CARTA::AddRequiredTiles AddRequiredTiles(
        int32_t file_id, CARTA::CompressionType compression_type, float compression_quality, const std::vector<int32_t>& tiles);
    static CARTA::Point Point(float x, float y);
    static CARTA::Point Point(const casacore::Vector<casacore::Double>& input, int x_index = 0, int y_index = 1);
    static CARTA::Point Point(const std::vector<casacore::Quantity>& input, int x_index = 0, int y_index = 1);
    static CARTA::Point Point(const std::vector<double>& input, int x_index = 0, int y_index = 1);
    static CARTA::SetRegion SetRegion(int32_t file_id, int32_t region_id, const CARTA::RegionInfo& region_info);
    static CARTA::ImageBounds ImageBounds(int32_t x_min, int32_t x_max, int32_t y_min, int32_t y_max);
    static CARTA::ConcatStokesFiles ConcatStokesFiles(
        int32_t file_id, const google::protobuf::RepeatedPtrField<CARTA::StokesFile>& stokes_files);
    static CARTA::DoublePoint DoublePoint(double x, double y);
    static CARTA::GaussianComponent GaussianComponent(
        const CARTA::DoublePoint& center, double amp, const CARTA::DoublePoint& fwhm, double pa);
    static CARTA::ScriptingRequest ScriptingRequest(uint32_t scripting_request_id, const std::string& target, const std::string& action,
        const std::string& parameters, bool async, const std::string& return_path);
    /// Response messages
    static CARTA::SpectralProfileData SpectralProfileData(int32_t file_id, int32_t region_id, int32_t stokes, float progress,
        std::string& coordinate, std::vector<CARTA::StatsType>& required_stats,
        std::map<CARTA::StatsType, std::vector<double>>& spectral_data);
    static CARTA::SpectralProfileData SpectralProfileData(int32_t stokes, float progress);
    static CARTA::SpatialProfileData SpatialProfileData(int32_t file_id, int32_t region_id, int32_t x, int32_t y, int32_t channel,
        int32_t stokes, float value, int32_t start, int32_t end, std::vector<float>& profile, std::string& coordinate, int32_t mip,
        CARTA::ProfileAxisType axis_type, float crpix, float crval, float cdelt, std::string& unit);
    static CARTA::SpatialProfileData SpatialProfileData(int32_t x, int32_t y, int32_t channel, int32_t stokes, float value);
    static CARTA::RasterTileSync RasterTileSync(
        int32_t file_id, int32_t channel, int32_t stokes, int32_t sync_id, int32_t animation_id, int32_t tile_count, bool end_sync);
    static CARTA::SetRegionAck SetRegionAck(int32_t region_id, bool success, std::string err_message);
    static CARTA::RegisterViewerAck RegisterViewerAck(
        uint32_t session_id, bool success, const std::string& status, const CARTA::SessionType& type);
    static CARTA::MomentProgress MomentProgress(int32_t file_id, float progress);
    static CARTA::PvProgress PvProgress(int32_t file_id, float progress, int32_t preview_id = 0);
    static CARTA::FittingProgress FittingProgress(int32_t file_id, float progress);
    static CARTA::RegionHistogramData RegionHistogramData(
        int32_t file_id, int32_t region_id, int32_t channel, int32_t stokes, float progress, const carta::HistogramConfig& hist_config);
    static CARTA::ContourImageData ContourImageData(
        int32_t file_id, uint32_t reference_file_id, int32_t channel, int32_t stokes, double progress);
    static CARTA::VectorOverlayTileData VectorOverlayTileData(int32_t file_id, int32_t channel, int32_t stokes_intensity,
        int32_t stokes_angle, const CARTA::CompressionType& compression_type, float compression_quality);
    static CARTA::ErrorData ErrorData(const std::string& message, std::vector<std::string> tags, CARTA::ErrorSeverity severity);
    static CARTA::FileInfo FileInfo(const std::string& name, CARTA::FileType type, int64_t size = 0, const std::string& hdu = "");
    static CARTA::RasterTileData RasterTileData(int32_t file_id, int32_t sync_id, int32_t animation_id);
    static CARTA::StartAnimationAck StartAnimationAck(bool success, int32_t animation_id, const std::string& message);
    static CARTA::ImportRegionAck ImportRegionAck(bool success, const std::string& message);
    static CARTA::RegionStatsData RegionStatsData(int32_t file_id, int32_t region_id, int32_t channel, int32_t stokes);
    static CARTA::Beam Beam(int32_t channel, int32_t stokes, float major_axis, float minor_axis, float pa);
    static CARTA::ListProgress ListProgress(
        const CARTA::FileListType& file_list_type, int32_t total_count, int32_t checked_count, float percentage);
    static CARTA::ImportRegionAck AddImportedRegion(CARTA::ImportRegionAck& import_ack, int region_id, CARTA::RegionType region_type,
        std::vector<CARTA::Point>& control_points, float region_rotation, CARTA::RegionStyle region_style);
    static CARTA::HeaderEntry* AddHeaderEntry(CARTA::FileInfoExtended& response, std::string name, const std::string& value,
        CARTA::EntryType type = CARTA::EntryType::STRING, double numeric_value = 0.0);
    static CARTA::HeaderEntry* AddComputedEntry(CARTA::FileInfoExtended& response, std::string name, const std::string& value,
        CARTA::EntryType type = CARTA::EntryType::STRING, double numeric_value = 0.0);
    static CARTA::FileInfoExtended SetDimensions(
        CARTA::FileInfoExtended& response, int32_t dimensions, int32_t width, int32_t height, int32_t depth, int32_t stokes);
    static CARTA::AxesNumbers* AddAxesNumbers(
        CARTA::FileInfoExtended& response, int32_t spatial_x, int32_t spatial_y, int32_t spectral, int32_t stokes, int32_t depth);

    // Decode messages
    static carta::EventHeader GetEventHeader(std::string_view message);

    /**
     * @brief Decodes a message from a buffer of characters into an object of type T and
     * can be used to decode various types of messages.
     *
     * @tparam T The type of the object to decode the message into. T must have a member function
     *           `ParseFromArray(const void*, int)` to parse the data.
     * @param sv_message The message to decode.
     * @throws std::runtime_error If the message cannot be parsed.
     * @return The decoded message of type T.
     */
    template <typename T>
    static T DecodeMessage(std::string_view sv_message);

    /**
     * @brief Encodes a protobuf message with a CARTA event header for transmission.
     *
     * @param event_type The CARTA event type to encode in the header.
     * @param event_id The event or request ID to encode in the header.
     * @param message The protobuf message to serialize and encode.
     * @return A std::vector<char> containing the header followed by the serialized message.
     */
    static std::vector<char> EncodeMessage(CARTA::EventType event_type, uint32_t event_id, const google::protobuf::MessageLite& message);
};

void FillHistogram(CARTA::Histogram* histogram, int32_t num_bins, double bin_width, double first_bin_center,
    const std::vector<int32_t>& bins, double mean, double std_dev);
void FillHistogram(CARTA::Histogram* histogram, const carta::BasicStats<float>& stats, const carta::Histogram& hist);
void FillStatistics(CARTA::RegionStatsData& stats_data, const std::vector<CARTA::StatsType>& required_stats,
    std::map<CARTA::StatsType, double>& stats_value_map);

#include "Message.tcc"

#endif // CARTA_SRC_UTIL_MESSAGE_H_
