/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "DummyBackend.h"
#include "OnMessageTask.h"

#include <chrono>
#include <thread>

static const uint16_t DUMMY_ICD_VERSION(ICD_VERSION);
static const uint32_t DUMMY_REQUEST_ID(0);

DummyBackend::DummyBackend() {
    uint32_t session_id(0);
    std::string address;
    std::string top_level_folder("/");
    std::string starting_folder("data/images");
    int grpc_port(-1);
    bool read_only_mode(false);

    _file_list_handler = new FileListHandler(top_level_folder, starting_folder);
    _session = new Session(
        nullptr, nullptr, session_id, address, top_level_folder, starting_folder, _file_list_handler, grpc_port, read_only_mode);

    _session->IncreaseRefCount(); // increase the reference count to avoid being deleted by the OnMessageTask
}

DummyBackend::~DummyBackend() {
    if (_session) {
        _session->WaitForTaskCancellation();
        if (!_session->DecreaseRefCount()) {
            delete _session;
        }
    }
    delete _file_list_handler;
}

void DummyBackend::ReceiveMessage(CARTA::RegisterViewer message) {
    _session->OnRegisterViewer(message, DUMMY_ICD_VERSION, DUMMY_REQUEST_ID);
}

void DummyBackend::ReceiveMessage(CARTA::ResumeSession message) {
    _session->OnResumeSession(message, DUMMY_REQUEST_ID);
}

void DummyBackend::ReceiveMessage(CARTA::SetImageChannels message) {
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

void DummyBackend::ReceiveMessage(CARTA::SetCursor message) {
    _session->AddCursorSetting(message, DUMMY_REQUEST_ID);
    OnMessageTask* tsk = new (tbb::task::allocate_root(_session->Context())) SetCursorTask(_session, message.file_id());
    if (tsk) {
        tbb::task::enqueue(*tsk);
    }
}

void DummyBackend::ReceiveMessage(CARTA::SetHistogramRequirements message) {
    OnMessageTask* tsk = nullptr;
    if (message.histograms_size() == 0) {
        _session->CancelSetHistRequirements();
    } else {
        _session->ResetHistContext();
        tsk = new (tbb::task::allocate_root(_session->HistContext())) SetHistogramRequirementsTask(_session, message, DUMMY_REQUEST_ID);
    }
    if (tsk) {
        tbb::task::enqueue(*tsk);
    }
}

void DummyBackend::ReceiveMessage(CARTA::CloseFile message) {
    _session->OnCloseFile(message);
}

void DummyBackend::ReceiveMessage(CARTA::StartAnimation message) {
    _session->CancelExistingAnimation();
    _session->BuildAnimationObject(message, DUMMY_REQUEST_ID);
    OnMessageTask* tsk = new (tbb::task::allocate_root(_session->AnimationContext())) AnimationTask(_session);
    if (tsk) {
        tbb::task::enqueue(*tsk);
    }
}

void DummyBackend::ReceiveMessage(CARTA::StopAnimation message) {
    _session->StopAnimation(message.file_id(), message.end_frame());
}

void DummyBackend::ReceiveMessage(CARTA::AnimationFlowControl message) {
    _session->HandleAnimationFlowControlEvt(message);
}

void DummyBackend::ReceiveMessage(CARTA::FileInfoRequest message) {
    _session->OnFileInfoRequest(message, DUMMY_REQUEST_ID);
}

void DummyBackend::ReceiveMessage(CARTA::OpenFile message) {
    _session->OnOpenFile(message, DUMMY_REQUEST_ID);
}

void DummyBackend::ReceiveMessage(CARTA::AddRequiredTiles message) {
    OnMessageTask* tsk = new (tbb::task::allocate_root(_session->Context())) OnAddRequiredTilesTask(_session, message);
    if (tsk) {
        tbb::task::enqueue(*tsk);
    }
}

void DummyBackend::ReceiveMessage(CARTA::RegionFileInfoRequest message) {
    _session->OnRegionFileInfoRequest(message, DUMMY_REQUEST_ID);
}

void DummyBackend::ReceiveMessage(CARTA::ImportRegion message) {
    _session->OnImportRegion(message, DUMMY_REQUEST_ID);
}

void DummyBackend::ReceiveMessage(CARTA::ExportRegion message) {
    _session->OnExportRegion(message, DUMMY_REQUEST_ID);
}

void DummyBackend::ReceiveMessage(CARTA::SetContourParameters message) {
    OnMessageTask* tsk = new (tbb::task::allocate_root(_session->Context())) OnSetContourParametersTask(_session, message);
    if (tsk) {
        tbb::task::enqueue(*tsk);
    }
}

void DummyBackend::ReceiveMessage(CARTA::ScriptingResponse message) {
    _session->OnScriptingResponse(message, DUMMY_REQUEST_ID);
}

void DummyBackend::ReceiveMessage(CARTA::SetRegion message) {
    _session->OnSetRegion(message, DUMMY_REQUEST_ID);
}

void DummyBackend::ReceiveMessage(CARTA::RemoveRegion message) {
    _session->OnRemoveRegion(message);
}

void DummyBackend::ReceiveMessage(CARTA::SetSpectralRequirements message) {
    _session->OnSetSpectralRequirements(message);
}

void DummyBackend::ReceiveMessage(CARTA::CatalogFileInfoRequest message) {
    _session->OnCatalogFileInfo(message, DUMMY_REQUEST_ID);
}

void DummyBackend::ReceiveMessage(CARTA::OpenCatalogFile message) {
    _session->OnOpenCatalogFile(message, DUMMY_REQUEST_ID);
}

void DummyBackend::ReceiveMessage(CARTA::CloseCatalogFile message) {
    _session->OnCloseCatalogFile(message);
}

void DummyBackend::ReceiveMessage(CARTA::CatalogFilterRequest message) {
    _session->OnCatalogFilter(message, DUMMY_REQUEST_ID);
}

void DummyBackend::ReceiveMessage(CARTA::StopMomentCalc message) {
    _session->OnStopMomentCalc(message);
}

void DummyBackend::ReceiveMessage(CARTA::SaveFile message) {
    _session->OnSaveFile(message, DUMMY_REQUEST_ID);
}

void DummyBackend::ReceiveMessage(CARTA::SplataloguePing message) {
    OnMessageTask* tsk = new (tbb::task::allocate_root(_session->Context())) OnSplataloguePingTask(_session, DUMMY_REQUEST_ID);
    if (tsk) {
        tbb::task::enqueue(*tsk);
    }
}

void DummyBackend::ReceiveMessage(CARTA::SpectralLineRequest message) {
    OnMessageTask* tsk = new (tbb::task::allocate_root(_session->Context())) OnSpectralLineRequestTask(_session, message, DUMMY_REQUEST_ID);
    if (tsk) {
        tbb::task::enqueue(*tsk);
    }
}

void DummyBackend::ReceiveMessage(CARTA::ConcatStokesFiles message) {
    _session->OnConcatStokesFiles(message, DUMMY_REQUEST_ID);
}

void DummyBackend::ReceiveMessage(CARTA::StopFileList message) {
    if (message.file_list_type() == CARTA::Image) {
        _session->StopImageFileList();
    } else {
        _session->StopCatalogFileList();
    }
}

void DummyBackend::ReceiveMessage(CARTA::SetSpatialRequirements message) {
    OnMessageTask* tsk = new (tbb::task::allocate_root(_session->Context())) GeneralMessageTask(_session, message, DUMMY_REQUEST_ID);
    if (tsk) {
        tbb::task::enqueue(*tsk);
    }
}

void DummyBackend::ReceiveMessage(CARTA::SetStatsRequirements message) {
    OnMessageTask* tsk = new (tbb::task::allocate_root(_session->Context())) GeneralMessageTask(_session, message, DUMMY_REQUEST_ID);
    if (tsk) {
        tbb::task::enqueue(*tsk);
    }
}

void DummyBackend::ReceiveMessage(CARTA::MomentRequest message) {
    OnMessageTask* tsk = new (tbb::task::allocate_root(_session->Context())) GeneralMessageTask(_session, message, DUMMY_REQUEST_ID);
    if (tsk) {
        tbb::task::enqueue(*tsk);
    }
}

void DummyBackend::ReceiveMessage(CARTA::FileListRequest message) {
    OnMessageTask* tsk = new (tbb::task::allocate_root(_session->Context())) GeneralMessageTask(_session, message, DUMMY_REQUEST_ID);
    if (tsk) {
        tbb::task::enqueue(*tsk);
    }
}

void DummyBackend::ReceiveMessage(CARTA::RegionListRequest message) {
    OnMessageTask* tsk = new (tbb::task::allocate_root(_session->Context())) GeneralMessageTask(_session, message, DUMMY_REQUEST_ID);
    if (tsk) {
        tbb::task::enqueue(*tsk);
    }
}

void DummyBackend::ReceiveMessage(CARTA::CatalogListRequest message) {
    OnMessageTask* tsk = new (tbb::task::allocate_root(_session->Context())) GeneralMessageTask(_session, message, DUMMY_REQUEST_ID);
    if (tsk) {
        tbb::task::enqueue(*tsk);
    }
}

//--------------------------------------------------------------

bool DummyBackend::TryPopMessagesQueue(std::pair<std::vector<char>, bool>& message) {
    return _session->TryPopMessagesQueue(message);
}

void DummyBackend::ClearMessagesQueue() {
    _session->ClearMessagesQueue();
}

void DummyBackend::WaitForJobFinished() {
    while (_session->GetRefCount() > 1) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}
