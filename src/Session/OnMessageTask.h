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
    std::shared_ptr<Session> _session;

public:
    OnMessageTask(std::shared_ptr<Session> session) : _session(session) {}
    virtual ~OnMessageTask() {
        spdlog::info("({}) Remove Session {} in ~OMT", fmt::ptr(_session), _session->GetId());
        _session_manager->DeleteSession(_session->GetId());
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
    SetImageChannelsTask(std::shared_ptr<Session> session, int file_id) : OnMessageTask(session), _file_id(file_id) {}
};

class SetCursorTask : public OnMessageTask {
    int _file_id;
    void execute() override;

public:
    SetCursorTask(std::shared_ptr<Session> session, int file_id) : OnMessageTask(session), _file_id(file_id) {}
};

class AnimationTask : public OnMessageTask {
    void execute() override;

public:
    AnimationTask(std::shared_ptr<Session> session) : OnMessageTask(session) {}
};

class StartAnimationTask : public OnMessageTask {
    void execute() override;
    CARTA::StartAnimation _msg;
    int _msg_id;

public:
    StartAnimationTask(std::shared_ptr<Session> session, CARTA::StartAnimation& msg, int id) : OnMessageTask(session) {
        _msg = msg;
        _msg_id = id;
    }
};

class RegionDataStreamsTask : public OnMessageTask {
    void execute() override;
    int _file_id, _region_id;

public:
    RegionDataStreamsTask(std::shared_ptr<Session> session, int file_id, int region_id)
        : OnMessageTask(session), _file_id(file_id), _region_id(region_id) {}
};

class SpectralProfileTask : public OnMessageTask {
    void execute() override;
    int _file_id, _region_id;

public:
    SpectralProfileTask(std::shared_ptr<Session> session, int file_id, int region_id) : OnMessageTask(session), _file_id(file_id), _region_id(region_id) {}
};

class PvPreviewUpdateTask : public OnMessageTask {
    void execute() override;
    int _file_id, _region_id;
    bool _preview_region;

public:
    PvPreviewUpdateTask(std::shared_ptr<Session> session, int file_id, int region_id, bool preview_region)
        : OnMessageTask(session), _file_id(file_id), _region_id(region_id), _preview_region(preview_region) {}
};

} // namespace carta

#include "OnMessageTask.tcc"

#endif // CARTA_SRC_SESSION_ONMESSAGETASK_H_
