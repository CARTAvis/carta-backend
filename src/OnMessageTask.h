/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
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
#include "EventHeader.h"
#include "Session.h"

class OnMessageTask : public tbb::task {
protected:
    Session* _session;
    tbb::task* execute() override = 0;

public:
    OnMessageTask(Session* session) {
        _session = session;
        _session->IncreaseRefCount();
    }
    ~OnMessageTask() {
        if (!_session->DecreaseRefCount())
            delete _session;
        _session = nullptr;
    }
};

class MultiMessageTask : public OnMessageTask {
    carta::EventHeader _header;
    int _event_length;
    char* _event_buffer;
    tbb::task* execute() override;

public:
    MultiMessageTask(Session* session_, carta::EventHeader& head, int evt_len, char* event_buf) : OnMessageTask(session_) {
        _header = head;
        _event_length = evt_len;
        _event_buffer = event_buf;
    }
    ~MultiMessageTask() {
        delete[] _event_buffer;
    };
};

class SetImageChannelsTask : public OnMessageTask {
    int fileId;
    tbb::task* execute() override;

public:
    SetImageChannelsTask(Session* session, int fileId) : OnMessageTask(session), fileId(fileId) {}
    ~SetImageChannelsTask() = default;
};

class SetCursorTask : public OnMessageTask {
    int _file_id;
    tbb::task* execute() override;

public:
    SetCursorTask(Session* session, int file_id) : OnMessageTask(session) {
        _file_id = file_id;
    }
    ~SetCursorTask() = default;
};

class SetHistogramRequirementsTask : public OnMessageTask {
    tbb::task* execute();
    carta::EventHeader _header;
    int _event_length;
    char* _event_buffer;

public:
    SetHistogramRequirementsTask(Session* session, carta::EventHeader& head, int len, char* buf) : OnMessageTask(session) {
        _header = head;
        _event_length = len;
        _event_buffer = buf;
    }
    ~SetHistogramRequirementsTask() = default;
};

class AnimationTask : public OnMessageTask {
    tbb::task* execute() override;

public:
    AnimationTask(Session* session) : OnMessageTask(session) {}
    ~AnimationTask() = default;
};

class OnAddRequiredTilesTask : public OnMessageTask {
    tbb::task* execute() override;
    CARTA::AddRequiredTiles _message;
    int _start, _stride, _end;

public:
    OnAddRequiredTilesTask(Session* session, CARTA::AddRequiredTiles message) : OnMessageTask(session) {
        _message = message;
    }
    ~OnAddRequiredTilesTask() = default;
};

class OnSetContourParametersTask : public OnMessageTask {
    tbb::task* execute() override;
    CARTA::SetContourParameters _message;
    int _start, _stride, _end;

public:
    OnSetContourParametersTask(Session* session, CARTA::SetContourParameters message) : OnMessageTask(session) {
        _message = message;
    }
    ~OnSetContourParametersTask() = default;
};

class RegionDataStreamsTask : public OnMessageTask {
    tbb::task* execute() override;
    int _file_id, _region_id;

public:
    RegionDataStreamsTask(Session* session, int file_id, int region_id) : OnMessageTask(session) {
        _file_id = file_id;
        _region_id = region_id;
    }
    ~RegionDataStreamsTask() = default;
};

class SpectralProfileTask : public OnMessageTask {
    tbb::task* execute() override;
    int _file_id, _region_id;

public:
    SpectralProfileTask(Session* session, int file_id, int region_id) : OnMessageTask(session) {
        _file_id = file_id;
        _region_id = region_id;
    }
    ~SpectralProfileTask() = default;
};

class OnSpectralLineRequestTask : public OnMessageTask {
    tbb::task* execute() override;
    CARTA::SpectralLineRequest _message;
    uint32_t _request_id;

public:
    OnSpectralLineRequestTask(Session* session, CARTA::SpectralLineRequest message, uint32_t request_id) : OnMessageTask(session) {
        _message = message;
        _request_id = request_id;
    }
    ~OnSpectralLineRequestTask() = default;
};

#endif // CARTA_BACKEND__ONMESSAGETASK_H_
