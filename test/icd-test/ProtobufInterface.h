/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_ICD_TEST_PROTOBUFINTERFACE_H_
#define CARTA_BACKEND_ICD_TEST_PROTOBUFINTERFACE_H_

#include "Session.h"

CARTA::RegisterViewer GetRegisterViewer(uint32_t session_id, string api_key, uint32_t client_feature_flags);
CARTA::CloseFile GetCloseFile(int32_t file_id);
CARTA::OpenFile GetOpenFile(string directory, string file, string hdu, int32_t file_id, CARTA::RenderMode render_mode);
CARTA::SetImageChannels GetSetImageChannels(
    int32_t file_id, int32_t channel, int32_t stokes, CARTA::CompressionType compression_type, float compression_quality);
CARTA::SetCursor GetSetCursor(int32_t file_id, float x, float y);
CARTA::SetSpatialRequirements GetSetSpatialRequirements(int32_t file_id, int32_t region_id);
CARTA::SetStatsRequirements GetSetStatsRequirements(int32_t file_id, int32_t region_id);
CARTA::SetHistogramRequirements GetSetHistogramRequirements(int32_t file_id, int32_t region_id);
CARTA::AddRequiredTiles GetAddRequiredTiles(
    int32_t file_id, CARTA::CompressionType compression_type, float compression_quality, const std::vector<float>& tiles);
CARTA::Point GetPoint(int x, int y);
CARTA::SetRegion GetSetRegion(
    int32_t file_id, int32_t region_id, CARTA::RegionType region_type, vector<CARTA::Point> control_points, float rotation);
CARTA::SetStatsRequirements GetSetStatsRequirements(int32_t file_id, int32_t region_id, string coordinate);
CARTA::SetSpectralRequirements GetSetSpectralRequirements(int32_t file_id, int32_t region_id, string coordinate);
CARTA::StartAnimation GetStartAnimation(int32_t file_id, std::pair<int32_t, int32_t> first_frame, std::pair<int32_t, int32_t> start_frame,
    std::pair<int32_t, int32_t> last_frame, std::pair<int32_t, int32_t> delta_frame, CARTA::CompressionType compression_type,
    float compression_quality, const std::vector<float>& tiles);
CARTA::AnimationFlowControl GetAnimationFlowControl(int32_t file_id, std::pair<int32_t, int32_t> received_frame);
CARTA::StopAnimation GetStopAnimation(int32_t file_id, std::pair<int32_t, int32_t> end_frame);

CARTA::EventType GetEventType(std::vector<char>& message);
void LogRequestedEventType(const CARTA::EventType& event_type);
void LogResponsiveEventType(const CARTA::EventType& event_type);

template <typename T>
T DecodeMessage(std::vector<char>& message) {
    T decoded_message;
    char* event_buf = message.data() + sizeof(carta::EventHeader);
    int event_length = message.size() - sizeof(carta::EventHeader);
    decoded_message.ParseFromArray(event_buf, event_length);
    return decoded_message;
}

#endif // CARTA_BACKEND_ICD_TEST_PROTOBUFINTERFACE_H_
