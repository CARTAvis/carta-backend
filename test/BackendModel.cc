/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "BackendModel.h"
#include "ThreadingManager/ThreadingManager.h"
#include "Util/Message.h"

#include <chrono>
#include <thread>

static const uint16_t DUMMY_ICD_VERSION(ICD_VERSION);
static const uint32_t DUMMY_REQUEST_ID(0);

bool TestSession::TryPopMessagesQueue(std::pair<std::vector<char>, bool>& message) {
    return _out_msgs.try_pop(message);
}

void TestSession::ClearMessagesQueue() {
    _out_msgs.clear();
}

std::unique_ptr<BackendModel> BackendModel::GetDummyBackend() {
    uint32_t session_id(0);
    std::string address;
    std::string top_level_folder("/");
    std::string starting_folder("data/images");
    bool read_only_mode(false);
    bool enable_scripting(false);

    return std::make_unique<BackendModel>(
        nullptr, nullptr, session_id, address, top_level_folder, starting_folder, read_only_mode, enable_scripting);
}

BackendModel::BackendModel(uWS::WebSocket<false, true, PerSocketData>* ws, uWS::Loop* loop, uint32_t session_id, std::string address,
    std::string top_level_folder, std::string starting_folder, bool read_only_mode, bool enable_scripting) {
    _file_list_handler = std::make_shared<FileListHandler>(top_level_folder, starting_folder);
    _session =
        new TestSession(session_id, address, top_level_folder, starting_folder, _file_list_handler, read_only_mode, enable_scripting);

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
}

void BackendModel::Receive(CARTA::RegisterViewer message) {
    carta::logger::LogReceivedEventType(CARTA::EventType::REGISTER_VIEWER);
    _session->OnRegisterViewer(message, DUMMY_ICD_VERSION, DUMMY_REQUEST_ID);
}

void BackendModel::Receive(CARTA::ResumeSession message) {
    carta::logger::LogReceivedEventType(CARTA::EventType::RESUME_SESSION);
    _session->OnResumeSession(message, DUMMY_REQUEST_ID);
}

void BackendModel::Receive(CARTA::SetImageChannels message) {
    carta::logger::LogReceivedEventType(CARTA::EventType::SET_IMAGE_CHANNELS);
    OnMessageTask* tsk = nullptr;
    _session->ImageChannelLock(message.file_id());
    if (!_session->ImageChannelTaskTestAndSet(message.file_id())) {
        tsk = new SetImageChannelsTask(_session, message.file_id());
    }
    // has its own queue to keep channels in order during animation
    _session->AddToSetChannelQueue(message, DUMMY_REQUEST_ID);
    _session->ImageChannelUnlock(message.file_id());
    if (tsk) {
        ThreadManager::QueueTask(tsk);
    }
}

void BackendModel::Receive(CARTA::SetCursor message) {
    carta::logger::LogReceivedEventType(CARTA::EventType::SET_CURSOR);
    _session->AddCursorSetting(message, DUMMY_REQUEST_ID);
    OnMessageTask* tsk = new SetCursorTask(_session, message.file_id());
    ThreadManager::QueueTask(tsk);
}

void BackendModel::Receive(CARTA::SetHistogramRequirements message) {
    carta::logger::LogReceivedEventType(CARTA::EventType::SET_HISTOGRAM_REQUIREMENTS);
    if (message.histograms_size() == 0) {
        _session->CancelSetHistRequirements();
    } else {
        _session->ResetHistContext();
        OnMessageTask* tsk = new GeneralMessageTask<CARTA::SetHistogramRequirements>(_session, message, DUMMY_REQUEST_ID);
        ThreadManager::QueueTask(tsk);
    }
}

void BackendModel::Receive(CARTA::CloseFile message) {
    carta::logger::LogReceivedEventType(CARTA::EventType::CLOSE_FILE);
    _session->OnCloseFile(message);
}

void BackendModel::Receive(CARTA::StartAnimation message) {
    carta::logger::LogReceivedEventType(CARTA::EventType::START_ANIMATION);
    _session->CancelExistingAnimation();
    OnMessageTask* tsk = new StartAnimationTask(_session, message, DUMMY_REQUEST_ID);
    ThreadManager::QueueTask(tsk);
}

void BackendModel::Receive(CARTA::StopAnimation message) {
    carta::logger::LogReceivedEventType(CARTA::EventType::STOP_ANIMATION);
    _session->StopAnimation(message.file_id(), message.end_frame());
}

void BackendModel::Receive(CARTA::AnimationFlowControl message) {
    carta::logger::LogReceivedEventType(CARTA::EventType::ANIMATION_FLOW_CONTROL);
    _session->HandleAnimationFlowControlEvt(message);
}

void BackendModel::Receive(CARTA::FileInfoRequest message) {
    carta::logger::LogReceivedEventType(CARTA::EventType::FILE_INFO_REQUEST);
    _session->OnFileInfoRequest(message, DUMMY_REQUEST_ID);
}

void BackendModel::Receive(CARTA::OpenFile message) {
    carta::logger::LogReceivedEventType(CARTA::EventType::OPEN_FILE);
    _session->CloseCachedImage(message.directory(), message.file());
    _session->OnOpenFile(message, DUMMY_REQUEST_ID);
}

void BackendModel::Receive(CARTA::AddRequiredTiles message) {
    carta::logger::LogReceivedEventType(CARTA::EventType::ADD_REQUIRED_TILES);
    OnMessageTask* tsk = new GeneralMessageTask<CARTA::AddRequiredTiles>(_session, message, DUMMY_REQUEST_ID);
    ThreadManager::QueueTask(tsk);
}

void BackendModel::Receive(CARTA::RegionFileInfoRequest message) {
    carta::logger::LogReceivedEventType(CARTA::EventType::REGION_FILE_INFO_REQUEST);
    _session->OnRegionFileInfoRequest(message, DUMMY_REQUEST_ID);
}

void BackendModel::Receive(CARTA::ImportRegion message) {
    carta::logger::LogReceivedEventType(CARTA::EventType::IMPORT_REGION);
    _session->OnImportRegion(message, DUMMY_REQUEST_ID);
}

void BackendModel::Receive(CARTA::ExportRegion message) {
    carta::logger::LogReceivedEventType(CARTA::EventType::EXPORT_REGION);
    _session->OnExportRegion(message, DUMMY_REQUEST_ID);
}

void BackendModel::Receive(CARTA::SetContourParameters message) {
    carta::logger::LogReceivedEventType(CARTA::EventType::SET_CONTOUR_PARAMETERS);
    OnMessageTask* tsk = new GeneralMessageTask<CARTA::SetContourParameters>(_session, message, DUMMY_REQUEST_ID);
    ThreadManager::QueueTask(tsk);
}

void BackendModel::Receive(CARTA::ScriptingResponse message) {
    carta::logger::LogReceivedEventType(CARTA::EventType::SCRIPTING_RESPONSE);
    _session->OnScriptingResponse(message, DUMMY_REQUEST_ID);
}

void BackendModel::Receive(CARTA::SetRegion message) {
    carta::logger::LogReceivedEventType(CARTA::EventType::SET_REGION);
    _session->OnSetRegion(message, DUMMY_REQUEST_ID);
}

void BackendModel::Receive(CARTA::RemoveRegion message) {
    carta::logger::LogReceivedEventType(CARTA::EventType::REMOVE_REGION);
    _session->OnRemoveRegion(message);
}

void BackendModel::Receive(CARTA::SetSpectralRequirements message) {
    carta::logger::LogReceivedEventType(CARTA::EventType::SET_SPECTRAL_REQUIREMENTS);
    _session->OnSetSpectralRequirements(message);
}

void BackendModel::Receive(CARTA::CatalogFileInfoRequest message) {
    carta::logger::LogReceivedEventType(CARTA::EventType::CATALOG_FILE_INFO_REQUEST);
    _session->OnCatalogFileInfo(message, DUMMY_REQUEST_ID);
}

void BackendModel::Receive(CARTA::OpenCatalogFile message) {
    carta::logger::LogReceivedEventType(CARTA::EventType::OPEN_CATALOG_FILE);
    _session->OnOpenCatalogFile(message, DUMMY_REQUEST_ID);
}

void BackendModel::Receive(CARTA::CloseCatalogFile message) {
    carta::logger::LogReceivedEventType(CARTA::EventType::CLOSE_CATALOG_FILE);
    _session->OnCloseCatalogFile(message);
}

void BackendModel::Receive(CARTA::CatalogFilterRequest message) {
    carta::logger::LogReceivedEventType(CARTA::EventType::CATALOG_FILTER_REQUEST);
    _session->OnCatalogFilter(message, DUMMY_REQUEST_ID);
}

void BackendModel::Receive(CARTA::StopMomentCalc message) {
    carta::logger::LogReceivedEventType(CARTA::EventType::STOP_MOMENT_CALC);
    _session->OnStopMomentCalc(message);
}

void BackendModel::Receive(CARTA::SaveFile message) {
    carta::logger::LogReceivedEventType(CARTA::EventType::SAVE_FILE);
    _session->OnSaveFile(message, DUMMY_REQUEST_ID);
}

void BackendModel::Receive(CARTA::ConcatStokesFiles message) {
    carta::logger::LogReceivedEventType(CARTA::EventType::CONCAT_STOKES_FILES);
    _session->OnConcatStokesFiles(message, DUMMY_REQUEST_ID);
}

void BackendModel::Receive(CARTA::StopFileList message) {
    carta::logger::LogReceivedEventType(CARTA::EventType::STOP_FILE_LIST);
    if (message.file_list_type() == CARTA::Image) {
        _session->StopImageFileList();
    } else {
        _session->StopCatalogFileList();
    }
}

void BackendModel::Receive(CARTA::SetSpatialRequirements message) {
    carta::logger::LogReceivedEventType(CARTA::EventType::SET_SPATIAL_REQUIREMENTS);
    OnMessageTask* tsk = new GeneralMessageTask<CARTA::SetSpatialRequirements>(_session, message, DUMMY_REQUEST_ID);
    ThreadManager::QueueTask(tsk);
}

void BackendModel::Receive(CARTA::SetStatsRequirements message) {
    carta::logger::LogReceivedEventType(CARTA::EventType::SET_STATS_REQUIREMENTS);
    OnMessageTask* tsk = new GeneralMessageTask<CARTA::SetStatsRequirements>(_session, message, DUMMY_REQUEST_ID);
    ThreadManager::QueueTask(tsk);
}

void BackendModel::Receive(CARTA::MomentRequest message) {
    carta::logger::LogReceivedEventType(CARTA::EventType::MOMENT_REQUEST);
    OnMessageTask* tsk = new GeneralMessageTask<CARTA::MomentRequest>(_session, message, DUMMY_REQUEST_ID);
    ThreadManager::QueueTask(tsk);
}

void BackendModel::Receive(CARTA::FileListRequest message) {
    carta::logger::LogReceivedEventType(CARTA::EventType::FILE_LIST_REQUEST);
    OnMessageTask* tsk = new GeneralMessageTask<CARTA::FileListRequest>(_session, message, DUMMY_REQUEST_ID);
    ThreadManager::QueueTask(tsk);
}

void BackendModel::Receive(CARTA::RegionListRequest message) {
    carta::logger::LogReceivedEventType(CARTA::EventType::REGION_LIST_REQUEST);
    OnMessageTask* tsk = new GeneralMessageTask<CARTA::RegionListRequest>(_session, message, DUMMY_REQUEST_ID);
    ThreadManager::QueueTask(tsk);
}

void BackendModel::Receive(CARTA::CatalogListRequest message) {
    carta::logger::LogReceivedEventType(CARTA::EventType::CATALOG_LIST_REQUEST);
    OnMessageTask* tsk = new GeneralMessageTask<CARTA::CatalogListRequest>(_session, message, DUMMY_REQUEST_ID);
    ThreadManager::QueueTask(tsk);
}

void BackendModel::Receive(CARTA::SetVectorOverlayParameters message) {
    carta::logger::LogReceivedEventType(CARTA::EventType::SET_VECTOR_OVERLAY_PARAMETERS);
    OnMessageTask* tsk = new GeneralMessageTask<CARTA::SetVectorOverlayParameters>(_session, message, DUMMY_REQUEST_ID);
    ThreadManager::QueueTask(tsk);
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
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
