/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_ICD_TEST_DUMMYBACKEND_H_
#define CARTA_BACKEND_ICD_TEST_DUMMYBACKEND_H_

#include "OnMessageTask.h"
#include "Session.h"

template <typename T>
class GeneralMessageTask : public OnMessageTask {
    tbb::task* execute() {
        if constexpr (std::is_same_v<T, CARTA::SetSpatialRequirements>) {
            _session->OnSetSpatialRequirements(_message);
        } else if constexpr (std::is_same_v<T, CARTA::SetStatsRequirements>) {
            _session->OnSetStatsRequirements(_message);
        } else if constexpr (std::is_same_v<T, CARTA::MomentRequest>) {
            _session->OnMomentRequest(_message, _request_id);
        } else if constexpr (std::is_same_v<T, CARTA::FileListRequest>) {
            _session->OnFileListRequest(_message, _request_id);
        } else if constexpr (std::is_same_v<T, CARTA::RegionListRequest>) {
            _session->OnRegionListRequest(_message, _request_id);
        } else if constexpr (std::is_same_v<T, CARTA::CatalogListRequest>) {
            _session->OnCatalogFileList(_message, _request_id);
        } else {
            spdlog::warn("Bad event type in GeneralMessageType!");
        }
        return nullptr;
    };

    T _message;
    uint32_t _request_id;

public:
    GeneralMessageTask(Session* session, T message, uint32_t request_id)
        : OnMessageTask(session), _message(message), _request_id(request_id) {}

    ~GeneralMessageTask() = default;
};

class BackendModel {
public:
    BackendModel(uWS::WebSocket<false, true, PerSocketData>* ws, uWS::Loop* loop, uint32_t session_id, std::string address,
        std::string top_level_folder, std::string starting_folder, int grpc_port, bool read_only_mode);
    ~BackendModel();

    void ReceiveMessage(CARTA::RegisterViewer message);
    void ReceiveMessage(CARTA::ResumeSession message);
    void ReceiveMessage(CARTA::SetImageChannels message);
    void ReceiveMessage(CARTA::SetCursor message);
    void ReceiveMessage(CARTA::SetHistogramRequirements message);
    void ReceiveMessage(CARTA::CloseFile message);
    void ReceiveMessage(CARTA::StartAnimation message);
    void ReceiveMessage(CARTA::StopAnimation message);
    void ReceiveMessage(CARTA::AnimationFlowControl message);
    void ReceiveMessage(CARTA::FileInfoRequest message);
    void ReceiveMessage(CARTA::OpenFile message);
    void ReceiveMessage(CARTA::AddRequiredTiles message);
    void ReceiveMessage(CARTA::RegionFileInfoRequest message);
    void ReceiveMessage(CARTA::ImportRegion message);
    void ReceiveMessage(CARTA::ExportRegion message);
    void ReceiveMessage(CARTA::SetContourParameters message);
    void ReceiveMessage(CARTA::ScriptingResponse message);
    void ReceiveMessage(CARTA::SetRegion message);
    void ReceiveMessage(CARTA::RemoveRegion message);
    void ReceiveMessage(CARTA::SetSpectralRequirements message);
    void ReceiveMessage(CARTA::CatalogFileInfoRequest message);
    void ReceiveMessage(CARTA::OpenCatalogFile message);
    void ReceiveMessage(CARTA::CloseCatalogFile message);
    void ReceiveMessage(CARTA::CatalogFilterRequest message);
    void ReceiveMessage(CARTA::StopMomentCalc message);
    void ReceiveMessage(CARTA::SaveFile message);
    void ReceiveMessage(CARTA::SplataloguePing message);
    void ReceiveMessage(CARTA::SpectralLineRequest message);
    void ReceiveMessage(CARTA::ConcatStokesFiles message);
    void ReceiveMessage(CARTA::StopFileList message);
    void ReceiveMessage(CARTA::SetSpatialRequirements message);
    void ReceiveMessage(CARTA::SetStatsRequirements message);
    void ReceiveMessage(CARTA::MomentRequest message);
    void ReceiveMessage(CARTA::FileListRequest message);
    void ReceiveMessage(CARTA::RegionListRequest message);
    void ReceiveMessage(CARTA::CatalogListRequest message);

    bool TryPopMessagesQueue(std::pair<std::vector<char>, bool>& message);
    void ClearMessagesQueue();
    void WaitForJobFinished(); // wait for parallel calculations finished

private:
    FileListHandler* _file_list_handler;
    Session* _session;
};

#endif // CARTA_BACKEND_ICD_TEST_DUMMYBACKEND_H_
