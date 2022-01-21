/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# OnMessageTask.h: dequeues messages and calls appropriate Session handlers

#ifndef CARTA_BACKEND__ONMESSAGETASK_H_
#define CARTA_BACKEND__ONMESSAGETASK_H_

#include <string>
#include <tuple>
#include <vector>

#include <carta-protobuf/contour.pb.h>

#include "AnimationObject.h"
#include "Session.h"
#include "Util/Message.h"

namespace carta {

class OnMessageTask {
protected:
    Session* _session;

public:
    OnMessageTask(Session* session) : _session(session) {
        _session->IncreaseRefCount();
    }
    virtual ~OnMessageTask() {
        if (!_session->DecreaseRefCount()) {
            delete _session;
        }
        _session = nullptr;
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

class OnSplataloguePingTask : public OnMessageTask {
    OnMessageTask* execute() override;
    uint32_t _request_id;

public:
    OnSplataloguePingTask(Session* session, uint32_t request_id) : OnMessageTask(session), _request_id(request_id) {}
    ~OnSplataloguePingTask() = default;
};

} // namespace carta

#include "OnMessageTask.tcc"

#endif // CARTA_BACKEND__ONMESSAGETASK_H_
