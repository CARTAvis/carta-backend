/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "OnMessageTask.h"
#include "ThreadManager/ThreadManager.h"

#include <algorithm>

using namespace carta;

std::shared_ptr<SessionManager> OnMessageTask::_session_manager;

void SetImageChannelsTask::execute() {
    std::pair<CARTA::SetImageChannels, uint32_t> request_pair;
    bool tester;

    _session->ImageChannelLock(_file_id);
    tester = _session->_set_channel_queues[_file_id].try_pop(request_pair);
    _session->ImageChannelTaskSetIdle(_file_id);
    _session->ImageChannelUnlock(_file_id);

    if (tester) {
        _session->ExecuteSetChannelEvt(request_pair);
    }
}

void SetCursorTask::execute() {
    _session->_cursor_settings.ExecuteOne("SET_CURSOR", _file_id);
}

void AnimationTask::execute() {
    if (_session->ExecuteAnimationFrame()) {
        if (_session->CalculateAnimationFlowWindow() > _session->CurrentFlowWindowSize()) {
            _session->SetWaitingTask(true);
        } else {
            ThreadManager::QueueTask(new AnimationTask(_session));
        }
    }

    _session->SetAnimationActive(false);
}

void StartAnimationTask::execute() {
    if (_session->AnimationActive()) {
        ThreadManager::QueueTask(new StartAnimationTask(_session, _msg, _msg_id));
    } else {
        _session->SetAnimationActive(true);
        if (_session->BuildAnimationObject(_msg, _msg_id)) {
            ThreadManager::QueueTask(new AnimationTask(_session));
        } else {
            _session->SetAnimationActive(false);
        }
    }
}

void RegionDataStreamsTask::execute() {
    _session->RegionDataStreams(_file_id, _region_id);
}

void SpectralProfileTask::execute() {
    _session->SendSpectralProfileData(_file_id, _region_id);
}

void PvPreviewUpdateTask::execute() {
    _session->SendPvPreview(_file_id, _region_id, _preview_region);
}
