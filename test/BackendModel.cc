/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "BackendModel.h"
#include "ProtobufInterface.h"

#include <chrono>
#include <thread>

static const uint16_t DUMMY_ICD_VERSION(ICD_VERSION);
static const uint32_t DUMMY_REQUEST_ID(0);

std::unique_ptr<BackendModel> BackendModel::GetDummyBackend() {
    uint32_t session_id(0);
    std::string address;
    std::string top_level_folder("/");
    std::string starting_folder("data/images");
    int grpc_port(-1);
    bool read_only_mode(false);

    return std::make_unique<BackendModel>(
        nullptr, nullptr, session_id, address, top_level_folder, starting_folder, grpc_port, read_only_mode);
}

BackendModel::BackendModel(uWS::WebSocket<false, true, PerSocketData>* ws, uWS::Loop* loop, uint32_t session_id, std::string address,
    std::string top_level_folder, std::string starting_folder, int grpc_port, bool read_only_mode) {
    _file_list_handler = new FileListHandler(top_level_folder, starting_folder);
    _session = new Session(
        nullptr, nullptr, session_id, address, top_level_folder, starting_folder, _file_list_handler, grpc_port, read_only_mode);

    _session->IncreaseRefCount(); // increase the reference count to avoid being deleted by the OnMessageTask
}

BackendModel::~BackendModel() {
    if (_session) {
        spdlog::info(
            "Client {} [{}] Deleted. Remaining sessions: {}", _session->GetId(), _session->GetAddress(), Session::NumberOfSessions());
        _session->WaitForTaskCancellation();
        if (!_session->DecreaseRefCount()) {
            delete _session;
        } else {
            spdlog::warn("Session reference count is not 0 ({}) on deletion!", _session->GetRefCount());
        }
    }
    delete _file_list_handler;
}

void BackendModel::Receive(CARTA::RegisterViewer message) {
    LogRequestedEventType(CARTA::EventType::REGISTER_VIEWER);
    _session->OnRegisterViewer(message, DUMMY_ICD_VERSION, DUMMY_REQUEST_ID);
}

void BackendModel::Receive(CARTA::ResumeSession message) {
    LogRequestedEventType(CARTA::EventType::RESUME_SESSION);
    _session->OnResumeSession(message, DUMMY_REQUEST_ID);
}

void BackendModel::Receive(CARTA::SetImageChannels message) {
    LogRequestedEventType(CARTA::EventType::SET_IMAGE_CHANNELS);
    OnMessageTask* tsk = nullptr;
    _session->ImageChannelLock(message.file_id());
    if (!_session->ImageChannelTaskTestAndSet(message.file_id())) {
        tsk = new (tbb::task::allocate_root(_session->Context())) SetImageChannelsTask(_session, message.file_id());
    }
    // has its own queue to keep channels in order during animation
    _session->AddToSetChannelQueue(message, DUMMY_REQUEST_ID);
    _session->ImageChannelUnlock(message.file_id());
    if (tsk) {
        tbb::task::enqueue(*tsk);
    }
}

void BackendModel::Receive(CARTA::SetCursor message) {
    LogRequestedEventType(CARTA::EventType::SET_CURSOR);
    _session->AddCursorSetting(message, DUMMY_REQUEST_ID);
    OnMessageTask* tsk = new (tbb::task::allocate_root(_session->Context())) SetCursorTask(_session, message.file_id());
    tbb::task::enqueue(*tsk);
}

void BackendModel::Receive(CARTA::SetHistogramRequirements message) {
    LogRequestedEventType(CARTA::EventType::SET_HISTOGRAM_REQUIREMENTS);
    if (message.histograms_size() == 0) {
        _session->CancelSetHistRequirements();
    } else {
        _session->ResetHistContext();
        OnMessageTask* tsk =
            new (tbb::task::allocate_root(_session->HistContext())) SetHistogramRequirementsTask(_session, message, DUMMY_REQUEST_ID);
        tbb::task::enqueue(*tsk);
    }
}

void BackendModel::Receive(CARTA::CloseFile message) {
    LogRequestedEventType(CARTA::EventType::CLOSE_FILE);
    _session->OnCloseFile(message);
}

void BackendModel::Receive(CARTA::StartAnimation message) {
    LogRequestedEventType(CARTA::EventType::START_ANIMATION);
    _session->CancelExistingAnimation();
    _session->BuildAnimationObject(message, DUMMY_REQUEST_ID);
    OnMessageTask* tsk = new (tbb::task::allocate_root(_session->AnimationContext())) AnimationTask(_session);
    tbb::task::enqueue(*tsk);
}

void BackendModel::Receive(CARTA::StopAnimation message) {
    LogRequestedEventType(CARTA::EventType::STOP_ANIMATION);
    _session->StopAnimation(message.file_id(), message.end_frame());
}

void BackendModel::Receive(CARTA::AnimationFlowControl message) {
    LogRequestedEventType(CARTA::EventType::ANIMATION_FLOW_CONTROL);
    _session->HandleAnimationFlowControlEvt(message);
}

void BackendModel::Receive(CARTA::FileInfoRequest message) {
    LogRequestedEventType(CARTA::EventType::FILE_INFO_REQUEST);
    _session->OnFileInfoRequest(message, DUMMY_REQUEST_ID);
}

void BackendModel::Receive(CARTA::OpenFile message) {
    LogRequestedEventType(CARTA::EventType::OPEN_FILE);
    _session->OnOpenFile(message, DUMMY_REQUEST_ID);
}

void BackendModel::Receive(CARTA::AddRequiredTiles message) {
    LogRequestedEventType(CARTA::EventType::ADD_REQUIRED_TILES);
    OnMessageTask* tsk = new (tbb::task::allocate_root(_session->Context())) OnAddRequiredTilesTask(_session, message);
    tbb::task::enqueue(*tsk);
}

void BackendModel::Receive(CARTA::RegionFileInfoRequest message) {
    LogRequestedEventType(CARTA::EventType::REGION_FILE_INFO_REQUEST);
    _session->OnRegionFileInfoRequest(message, DUMMY_REQUEST_ID);
}

void BackendModel::Receive(CARTA::ImportRegion message) {
    LogRequestedEventType(CARTA::EventType::IMPORT_REGION);
    _session->OnImportRegion(message, DUMMY_REQUEST_ID);
}

void BackendModel::Receive(CARTA::ExportRegion message) {
    LogRequestedEventType(CARTA::EventType::EXPORT_REGION);
    _session->OnExportRegion(message, DUMMY_REQUEST_ID);
}

void BackendModel::Receive(CARTA::SetContourParameters message) {
    LogRequestedEventType(CARTA::EventType::SET_CONTOUR_PARAMETERS);
    OnMessageTask* tsk = new (tbb::task::allocate_root(_session->Context())) OnSetContourParametersTask(_session, message);
    tbb::task::enqueue(*tsk);
}

void BackendModel::Receive(CARTA::ScriptingResponse message) {
    LogRequestedEventType(CARTA::EventType::SCRIPTING_RESPONSE);
    _session->OnScriptingResponse(message, DUMMY_REQUEST_ID);
}

void BackendModel::Receive(CARTA::SetRegion message) {
    LogRequestedEventType(CARTA::EventType::SET_REGION);
    _session->OnSetRegion(message, DUMMY_REQUEST_ID);
}

void BackendModel::Receive(CARTA::RemoveRegion message) {
    LogRequestedEventType(CARTA::EventType::REMOVE_REGION);
    _session->OnRemoveRegion(message);
}

void BackendModel::Receive(CARTA::SetSpectralRequirements message) {
    LogRequestedEventType(CARTA::EventType::SET_SPECTRAL_REQUIREMENTS);
    _session->OnSetSpectralRequirements(message);
}

void BackendModel::Receive(CARTA::CatalogFileInfoRequest message) {
    LogRequestedEventType(CARTA::EventType::CATALOG_FILE_INFO_REQUEST);
    _session->OnCatalogFileInfo(message, DUMMY_REQUEST_ID);
}

void BackendModel::Receive(CARTA::OpenCatalogFile message) {
    LogRequestedEventType(CARTA::EventType::OPEN_CATALOG_FILE);
    _session->OnOpenCatalogFile(message, DUMMY_REQUEST_ID);
}

void BackendModel::Receive(CARTA::CloseCatalogFile message) {
    LogRequestedEventType(CARTA::EventType::CLOSE_CATALOG_FILE);
    _session->OnCloseCatalogFile(message);
}

void BackendModel::Receive(CARTA::CatalogFilterRequest message) {
    LogRequestedEventType(CARTA::EventType::CATALOG_FILTER_REQUEST);
    _session->OnCatalogFilter(message, DUMMY_REQUEST_ID);
}

void BackendModel::Receive(CARTA::StopMomentCalc message) {
    LogRequestedEventType(CARTA::EventType::STOP_MOMENT_CALC);
    _session->OnStopMomentCalc(message);
}

void BackendModel::Receive(CARTA::SaveFile message) {
    LogRequestedEventType(CARTA::EventType::SAVE_FILE);
    _session->OnSaveFile(message, DUMMY_REQUEST_ID);
}

void BackendModel::Receive(CARTA::SplataloguePing message) {
    LogRequestedEventType(CARTA::EventType::SPLATALOGUE_PING);
    OnMessageTask* tsk = new (tbb::task::allocate_root(_session->Context())) OnSplataloguePingTask(_session, DUMMY_REQUEST_ID);
    tbb::task::enqueue(*tsk);
}

void BackendModel::Receive(CARTA::SpectralLineRequest message) {
    LogRequestedEventType(CARTA::EventType::SPECTRAL_LINE_REQUEST);
    OnMessageTask* tsk = new (tbb::task::allocate_root(_session->Context())) OnSpectralLineRequestTask(_session, message, DUMMY_REQUEST_ID);
    tbb::task::enqueue(*tsk);
}

void BackendModel::Receive(CARTA::ConcatStokesFiles message) {
    LogRequestedEventType(CARTA::EventType::CONCAT_STOKES_FILES);
    _session->OnConcatStokesFiles(message, DUMMY_REQUEST_ID);
}

void BackendModel::Receive(CARTA::StopFileList message) {
    LogRequestedEventType(CARTA::EventType::STOP_FILE_LIST);
    if (message.file_list_type() == CARTA::Image) {
        _session->StopImageFileList();
    } else {
        _session->StopCatalogFileList();
    }
}

void BackendModel::Receive(CARTA::SetSpatialRequirements message) {
    LogRequestedEventType(CARTA::EventType::SET_SPATIAL_REQUIREMENTS);
    OnMessageTask* tsk = new (tbb::task::allocate_root(_session->Context()))
        GeneralMessageTask<CARTA::SetSpatialRequirements>(_session, message, DUMMY_REQUEST_ID);
    tbb::task::enqueue(*tsk);
}

void BackendModel::Receive(CARTA::SetStatsRequirements message) {
    LogRequestedEventType(CARTA::EventType::SET_STATS_REQUIREMENTS);
    OnMessageTask* tsk = new (tbb::task::allocate_root(_session->Context()))
        GeneralMessageTask<CARTA::SetStatsRequirements>(_session, message, DUMMY_REQUEST_ID);
    tbb::task::enqueue(*tsk);
}

void BackendModel::Receive(CARTA::MomentRequest message) {
    LogRequestedEventType(CARTA::EventType::MOMENT_REQUEST);
    OnMessageTask* tsk =
        new (tbb::task::allocate_root(_session->Context())) GeneralMessageTask<CARTA::MomentRequest>(_session, message, DUMMY_REQUEST_ID);
    tbb::task::enqueue(*tsk);
}

void BackendModel::Receive(CARTA::FileListRequest message) {
    LogRequestedEventType(CARTA::EventType::FILE_LIST_REQUEST);
    OnMessageTask* tsk =
        new (tbb::task::allocate_root(_session->Context())) GeneralMessageTask<CARTA::FileListRequest>(_session, message, DUMMY_REQUEST_ID);
    tbb::task::enqueue(*tsk);
}

void BackendModel::Receive(CARTA::RegionListRequest message) {
    LogRequestedEventType(CARTA::EventType::REGION_LIST_REQUEST);
    OnMessageTask* tsk = new (tbb::task::allocate_root(_session->Context()))
        GeneralMessageTask<CARTA::RegionListRequest>(_session, message, DUMMY_REQUEST_ID);
    tbb::task::enqueue(*tsk);
}

void BackendModel::Receive(CARTA::CatalogListRequest message) {
    LogRequestedEventType(CARTA::EventType::CATALOG_LIST_REQUEST);
    OnMessageTask* tsk = new (tbb::task::allocate_root(_session->Context()))
        GeneralMessageTask<CARTA::CatalogListRequest>(_session, message, DUMMY_REQUEST_ID);
    tbb::task::enqueue(*tsk);
}

//--------------------------------------------------------------

bool BackendModel::TryPopMessagesQueue(std::pair<std::vector<char>, bool>& message) {
    return _session->TryPopMessagesQueue(message);
}

void BackendModel::ClearMessagesQueue() {
    _session->ClearMessagesQueue();
}

void BackendModel::WaitForJobFinished() {
    while (_session->GetRefCount() > 1) {
        std::this_thread::sleep_for(std::chrono::nanoseconds(1));
    }
}
