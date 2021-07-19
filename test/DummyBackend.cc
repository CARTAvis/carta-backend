/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "DummyBackend.h"
#include "OnMessageTask.h"

static const uint16_t DUMMY_ICD_VERSION(ICD_VERSION);
static const uint32_t DUMMY_REQUEST_ID(0);

DummyBackend::DummyBackend() {
    uint32_t session_id(0);
    std::string address;
    std::string top_level_folder("/");
    std::string starting_folder("data/images");
    int grpc_port(-1);

    _file_list_handler = new FileListHandler(top_level_folder, starting_folder);
    _session = new Session(nullptr, nullptr, session_id, address, top_level_folder, starting_folder, _file_list_handler, grpc_port);
}

DummyBackend::~DummyBackend() {
    delete _session;
    delete _file_list_handler;
}

void DummyBackend::ReceiveMessage(CARTA::RegisterViewer message) {
    _session->OnRegisterViewer(message, DUMMY_ICD_VERSION, DUMMY_REQUEST_ID);
}

void DummyBackend::ReceiveMessage(CARTA::CloseFile message) {
    _session->OnCloseFile(message);
}

void DummyBackend::ReceiveMessage(CARTA::OpenFile message) {
    _session->OnOpenFile(message, DUMMY_REQUEST_ID);
}

void DummyBackend::ReceiveMessage(CARTA::SetImageChannels message) {
    _session->ImageChannelLock(message.file_id());
    OnMessageTask* tsk = nullptr;
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

void DummyBackend::CheckMessagesQueue(std::function<void(tbb::concurrent_queue<std::pair<std::vector<char>, bool>> out_msgs)> callback) {
    _session->CheckMessagesQueue(callback);
}
