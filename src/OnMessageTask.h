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
#include <tbb/concurrent_queue.h>
#include <tbb/task.h>

#include "AnimationObject.h"
#include "Session.h"
#include "Util/Message.h"

class OnMessageTask : public tbb::task {
protected:
    Session* _session;
    tbb::task* execute() override = 0;

public:
    OnMessageTask(Session* session) : _session(session) {
        _session->IncreaseRefCount();
    }
    ~OnMessageTask() {
        if (!_session->DecreaseRefCount()) {
            delete _session;
        }
        _session = nullptr;
    }
};

class SetImageChannelsTask : public OnMessageTask {
    int _file_id;
    tbb::task* execute() override;

public:
    SetImageChannelsTask(Session* session, int file_id) : OnMessageTask(session), _file_id(file_id) {}
    ~SetImageChannelsTask() = default;
};

class SetCursorTask : public OnMessageTask {
    int _file_id;
    tbb::task* execute() override;

public:
    SetCursorTask(Session* session, int file_id) : OnMessageTask(session), _file_id(file_id) {}
    ~SetCursorTask() = default;
};

class AnimationTask : public OnMessageTask {
    tbb::task* execute() override;

public:
    AnimationTask(Session* session) : OnMessageTask(session) {}
    ~AnimationTask() = default;
};

class RegionDataStreamsTask : public OnMessageTask {
    tbb::task* execute() override;
    int _file_id, _region_id;

public:
    RegionDataStreamsTask(Session* session, int file_id, int region_id)
        : OnMessageTask(session), _file_id(file_id), _region_id(region_id) {}
    ~RegionDataStreamsTask() = default;
};

class SpectralProfileTask : public OnMessageTask {
    tbb::task* execute() override;
    int _file_id, _region_id;

public:
    SpectralProfileTask(Session* session, int file_id, int region_id) : OnMessageTask(session), _file_id(file_id), _region_id(region_id) {}
    ~SpectralProfileTask() = default;
};

class OnSplataloguePingTask : public OnMessageTask {
    tbb::task* execute() override;
    uint32_t _request_id;

public:
    OnSplataloguePingTask(Session* session, uint32_t request_id) : OnMessageTask(session), _request_id(request_id) {}
    ~OnSplataloguePingTask() = default;
};

#include "OnMessageTask.tcc"

#endif // CARTA_BACKEND__ONMESSAGETASK_H_
