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

CARTA::EventType GetEventType(std::vector<char>& message);
CARTA::RegisterViewerAck GetRegisterViewerAck(std::vector<char>& message);
CARTA::OpenFileAck GetOpenFileAck(std::vector<char>& message);
CARTA::RegionHistogramData GetRegionHistogramData(std::vector<char>& message);
CARTA::RasterTileData GetRasterTileData(std::vector<char>& message);
CARTA::SpatialProfileData GetSpatialProfileData(std::vector<char>& message);
CARTA::RegionStatsData GetRegionStatsData(std::vector<char>& message);

#endif // CARTA_BACKEND_ICD_TEST_PROTOBUFINTERFACE_H_
