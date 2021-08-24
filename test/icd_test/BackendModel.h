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

    void Receive(CARTA::RegisterViewer message);
    void Receive(CARTA::ResumeSession message);
    void Receive(CARTA::SetImageChannels message);
    void Receive(CARTA::SetCursor message);
    void Receive(CARTA::SetHistogramRequirements message);
    void Receive(CARTA::CloseFile message);
    void Receive(CARTA::StartAnimation message);
    void Receive(CARTA::StopAnimation message);
    void Receive(CARTA::AnimationFlowControl message);
    void Receive(CARTA::FileInfoRequest message);
    void Receive(CARTA::OpenFile message);
    void Receive(CARTA::AddRequiredTiles message);
    void Receive(CARTA::RegionFileInfoRequest message);
    void Receive(CARTA::ImportRegion message);
    void Receive(CARTA::ExportRegion message);
    void Receive(CARTA::SetContourParameters message);
    void Receive(CARTA::ScriptingResponse message);
    void Receive(CARTA::SetRegion message);
    void Receive(CARTA::RemoveRegion message);
    void Receive(CARTA::SetSpectralRequirements message);
    void Receive(CARTA::CatalogFileInfoRequest message);
    void Receive(CARTA::OpenCatalogFile message);
    void Receive(CARTA::CloseCatalogFile message);
    void Receive(CARTA::CatalogFilterRequest message);
    void Receive(CARTA::StopMomentCalc message);
    void Receive(CARTA::SaveFile message);
    void Receive(CARTA::SplataloguePing message);
    void Receive(CARTA::SpectralLineRequest message);
    void Receive(CARTA::ConcatStokesFiles message);
    void Receive(CARTA::StopFileList message);
    void Receive(CARTA::SetSpatialRequirements message);
    void Receive(CARTA::SetStatsRequirements message);
    void Receive(CARTA::MomentRequest message);
    void Receive(CARTA::FileListRequest message);
    void Receive(CARTA::RegionListRequest message);
    void Receive(CARTA::CatalogListRequest message);

    bool TryPopMessagesQueue(std::pair<std::vector<char>, bool>& message);
    void WaitForJobFinished(); // wait for parallel calculations finished

private:
    FileListHandler* _file_list_handler;
    Session* _session;
};

#endif // CARTA_BACKEND_ICD_TEST_DUMMYBACKEND_H_
