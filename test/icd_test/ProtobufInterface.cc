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

//-------------------------------------------------------------------------

CARTA::EventType GetEventType(std::vector<char>& message) {
    carta::EventHeader head = *reinterpret_cast<const carta::EventHeader*>(message.data());
    return static_cast<CARTA::EventType>(head.type);
}

CARTA::RegisterViewerAck GetRegisterViewerAck(std::vector<char>& message) {
    CARTA::RegisterViewerAck register_viewer_ack;
    char* event_buf = message.data() + sizeof(carta::EventHeader);
    int event_length = message.size() - sizeof(carta::EventHeader);
    register_viewer_ack.ParseFromArray(event_buf, event_length);
    return register_viewer_ack;
}

CARTA::OpenFileAck GetOpenFileAck(std::vector<char>& message) {
    CARTA::OpenFileAck open_file_ack;
    char* event_buf = message.data() + sizeof(carta::EventHeader);
    int event_length = message.size() - sizeof(carta::EventHeader);
    open_file_ack.ParseFromArray(event_buf, event_length);
    return open_file_ack;
}

CARTA::RegionHistogramData GetRegionHistogramData(std::vector<char>& message) {
    CARTA::RegionHistogramData region_histogram_data;
    char* event_buf = message.data() + sizeof(carta::EventHeader);
    int event_length = message.size() - sizeof(carta::EventHeader);
    region_histogram_data.ParseFromArray(event_buf, event_length);
    return region_histogram_data;
}

CARTA::RasterTileData GetRasterTileData(std::vector<char>& message) {
    CARTA::RasterTileData raster_tile_data;
    char* event_buf = message.data() + sizeof(carta::EventHeader);
    int event_length = message.size() - sizeof(carta::EventHeader);
    raster_tile_data.ParseFromArray(event_buf, event_length);
    return raster_tile_data;
}
