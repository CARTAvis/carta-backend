/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "DummyBackend.h"

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

void DummyBackend::ReceiveMessage(CARTA::CloseFile message) {
    _session->OnCloseFile(message);
}

void DummyBackend::ReceiveMessage(CARTA::OpenFile message) {
    _session->OnOpenFile(message, DUMMY_REQUEST_ID);
}

void DummyBackend::ReceiveMessage(CARTA::SetImageChannels message) {
    _session->ImageChannelLock(message.file_id());
    _session->AddToSetChannelQueue(message, DUMMY_REQUEST_ID);
    _session->ImageChannelUnlock(message.file_id());

    _session->ImageChannelLock(message.file_id());
    std::pair<CARTA::SetImageChannels, uint32_t> request_pair;
    bool tester = _session->_set_channel_queues[message.file_id()].try_pop(request_pair);
    _session->ImageChannelTaskSetIdle(message.file_id());
    _session->ImageChannelUnlock(message.file_id());

    if (tester) {
        _session->ExecuteSetChannelEvt(request_pair);
    }
}

void DummyBackend::CheckMessagesQueue(std::function<void(tbb::concurrent_queue<std::pair<std::vector<char>, bool>> out_msgs)> callback) {
    _session->CheckMessagesQueue(callback);
}
