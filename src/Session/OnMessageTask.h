/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# OnMessageTask.h: dequeues messages and calls appropriate Session handlers

#ifndef CARTA_SRC_SESSION_ONMESSAGETASK_H_
#define CARTA_SRC_SESSION_ONMESSAGETASK_H_

#include <string>
#include <tuple>
#include <vector>

#include "AnimationObject.h"
#include "Session.h"
#include "SessionManager.h"
#include "ThreadManager/ThreadManager.h"
#include "Util/Message.h"

namespace carta {

class OnMessageTask : public Task {
private:
    static std::shared_ptr<SessionManager> _session_manager;

protected:
    Session* _session;

public:
    OnMessageTask(Session* session) : _session(session) {
        _session->IncreaseRefCount();
    }
    virtual ~OnMessageTask() {
        if (!_session->DecreaseRefCount()) {
            spdlog::info("({}) Remove Session {} in ~OMT", fmt::ptr(_session), _session->GetId());
            // Test here since the CARTA test system does not set this shared_ptr for all tests.
            if (_session_manager) {
                _session_manager->DeleteSession(_session->GetId());
            }
        }
        _session = nullptr;
    }
    static void SetSessionManager(shared_ptr<SessionManager>& session_manager) {
        _session_manager = session_manager;
    }
};

class SetImageChannelsTask : public OnMessageTask {
    int _file_id;
    void execute() override;

public:
    SetImageChannelsTask(Session* session, int file_id) : OnMessageTask(session), _file_id(file_id) {}
};

class SetCursorTask : public OnMessageTask {
    int _file_id;
    void execute() override;

public:
    SetCursorTask(Session* session, int file_id) : OnMessageTask(session), _file_id(file_id) {}
};

class AnimationTask : public OnMessageTask {
    void execute() override;

public:
    AnimationTask(Session* session) : OnMessageTask(session) {}
};

class StartAnimationTask : public OnMessageTask {
    void execute() override;
    CARTA::StartAnimation _msg;
    int _msg_id;

public:
    StartAnimationTask(Session* session, CARTA::StartAnimation& msg, int id) : OnMessageTask(session) {
        _msg = msg;
        _msg_id = id;
    }
};

class RegionDataStreamsTask : public OnMessageTask {
    void execute() override;
    int _file_id, _region_id;

public:
    RegionDataStreamsTask(Session* session, int file_id, int region_id)
        : OnMessageTask(session), _file_id(file_id), _region_id(region_id) {}
};

class SpectralProfileTask : public OnMessageTask {
    void execute() override;
    int _file_id, _region_id;

public:
    SpectralProfileTask(Session* session, int file_id, int region_id) : OnMessageTask(session), _file_id(file_id), _region_id(region_id) {}
};

class PvPreviewUpdateTask : public OnMessageTask {
    void execute() override;
    int _file_id, _region_id;
    bool _preview_region;

public:
    PvPreviewUpdateTask(Session* session, int file_id, int region_id, bool preview_region)
        : OnMessageTask(session), _file_id(file_id), _region_id(region_id), _preview_region(preview_region) {}
};

} // namespace carta

#include "OnMessageTask.tcc"

#endif // CARTA_SRC_SESSION_ONMESSAGETASK_H_
