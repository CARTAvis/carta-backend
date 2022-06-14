/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018 - 2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "OnMessageTask.h"
#include "ThreadingManager/ThreadingManager.h"

#include <algorithm>

using namespace carta;

std::shared_ptr<SessionManager> OnMessageTask::_session_manager;

OnMessageTask* SetImageChannelsTask::execute() {
    std::pair<CARTA::SetImageChannels, uint32_t> request_pair;
    bool tester;

    _session->ImageChannelLock(_file_id);
    tester = _session->_set_channel_queues[_file_id].try_pop(request_pair);
    _session->ImageChannelTaskSetIdle(_file_id);
    _session->ImageChannelUnlock(_file_id);

    if (tester) {
        _session->ExecuteSetChannelEvt(request_pair);
    }

    return nullptr;
}

OnMessageTask* SetCursorTask::execute() {
    _session->_file_settings.ExecuteOne("SET_CURSOR", _file_id);
    return nullptr;
}

OnMessageTask* AnimationTask::execute() {
    if (_session->ExecuteAnimationFrame()) {
        if (_session->CalculateAnimationFlowWindow() > _session->CurrentFlowWindowSize()) {
            _session->SetWaitingTask(true);
        } else {
            ThreadManager::QueueTask(new AnimationTask(_session));
        }
    }

    _session->SetAnimationActive(false);
    return nullptr;
}

OnMessageTask* StartAnimationTask::execute() {
    OnMessageTask* tsk;
    if (_session->AnimationActive()) {
        tsk = new StartAnimationTask(_session, _msg, _msg_id);
    } else {
        _session->SetAnimationActive(true);
        _session->BuildAnimationObject(_msg, _msg_id);
        tsk = new AnimationTask(_session);
    }
    ThreadManager::QueueTask(tsk);

    return nullptr;
}

OnMessageTask* RegionDataStreamsTask::execute() {
    _session->RegionDataStreams(_file_id, _region_id);
    return nullptr;
}

OnMessageTask* SpectralProfileTask::execute() {
    _session->SendSpectralProfileData(_file_id, _region_id);
    return nullptr;
}
