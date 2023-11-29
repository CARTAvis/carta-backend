/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
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
#include "Util/Message.h"

namespace carta {

class OnMessageTask {
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
    virtual OnMessageTask* execute() = 0;
};

class SetImageChannelsTask : public OnMessageTask {
    int _file_id;
    OnMessageTask* execute() override;

public:
    SetImageChannelsTask(Session* session, int file_id) : OnMessageTask(session), _file_id(file_id) {}
    ~SetImageChannelsTask() = default;
};

class SetCursorTask : public OnMessageTask {
    int _file_id;
    OnMessageTask* execute() override;

public:
    SetCursorTask(Session* session, int file_id) : OnMessageTask(session), _file_id(file_id) {}
    ~SetCursorTask() = default;
};

class AnimationTask : public OnMessageTask {
    OnMessageTask* execute() override;

public:
    AnimationTask(Session* session) : OnMessageTask(session) {}
    ~AnimationTask() = default;
};

class StartAnimationTask : public OnMessageTask {
    OnMessageTask* execute() override;
    CARTA::StartAnimation _msg;
    int _msg_id;

public:
    StartAnimationTask(Session* session, CARTA::StartAnimation& msg, int id) : OnMessageTask(session) {
        _msg = msg;
        _msg_id = id;
    }
    ~StartAnimationTask() = default;
};

class RegionDataStreamsTask : public OnMessageTask {
    OnMessageTask* execute() override;
    int _file_id, _region_id;

public:
    RegionDataStreamsTask(Session* session, int file_id, int region_id)
        : OnMessageTask(session), _file_id(file_id), _region_id(region_id) {}
    ~RegionDataStreamsTask() = default;
};

class SpectralProfileTask : public OnMessageTask {
    OnMessageTask* execute() override;
    int _file_id, _region_id;

public:
    SpectralProfileTask(Session* session, int file_id, int region_id) : OnMessageTask(session), _file_id(file_id), _region_id(region_id) {}
    ~SpectralProfileTask() = default;
};

class PvPreviewUpdateTask : public OnMessageTask {
    OnMessageTask* execute() override;
    int _file_id, _region_id;
    bool _preview_region;

public:
    PvPreviewUpdateTask(Session* session, int file_id, int region_id, bool preview_region)
        : OnMessageTask(session), _file_id(file_id), _region_id(region_id), _preview_region(preview_region) {}
    ~PvPreviewUpdateTask() = default;
};

} // namespace carta

#include "OnMessageTask.tcc"

#endif // CARTA_SRC_SESSION_ONMESSAGETASK_H_
