/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_ICD_TEST_DUMMYBACKEND_H_
#define CARTA_BACKEND_ICD_TEST_DUMMYBACKEND_H_

#include "Session/OnMessageTask.h"
#include "Session/Session.h"

class TestSession : public Session {
public:
    TestSession(uint32_t id, std::string address, std::string top_level_folder, std::string starting_folder,
        std::shared_ptr<FileListHandler> file_list_handler, bool read_only_mode = false, bool enable_scripting = false)
        : Session(nullptr, nullptr, id, address, top_level_folder, starting_folder, file_list_handler, read_only_mode, enable_scripting) {}

    bool TryPopMessagesQueue(std::pair<std::vector<char>, bool>& message);
    void ClearMessagesQueue();
};

class BackendModel {
public:
    BackendModel(uWS::WebSocket<false, true, PerSocketData>* ws, uWS::Loop* loop, uint32_t session_id, std::string address,
        std::string top_level_folder, std::string starting_folder, bool read_only_mode, bool enable_scripting);
    ~BackendModel();

    static std::unique_ptr<BackendModel> GetDummyBackend();

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
    void Receive(CARTA::ConcatStokesFiles message);
    void Receive(CARTA::StopFileList message);
    void Receive(CARTA::SetSpatialRequirements message);
    void Receive(CARTA::SetStatsRequirements message);
    void Receive(CARTA::MomentRequest message);
    void Receive(CARTA::FileListRequest message);
    void Receive(CARTA::RegionListRequest message);
    void Receive(CARTA::CatalogListRequest message);
    void Receive(CARTA::SetVectorOverlayParameters message);

    bool TryPopMessagesQueue(std::pair<std::vector<char>, bool>& message);
    void ClearMessagesQueue();
    void WaitForJobFinished(); // wait for parallel calculations finished

private:
    std::shared_ptr<FileListHandler> _file_list_handler;
    TestSession* _session;
};

#endif // CARTA_BACKEND_ICD_TEST_DUMMYBACKEND_H_
