//# OnMessageTask.h: dequeues messages and calls appropriate Session handlers

#ifndef CARTA_BACKEND__ONMESSAGETASK_H_
#define CARTA_BACKEND__ONMESSAGETASK_H_

#include <string>
#include <tuple>
#include <vector>

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

class SetImageChannelsTask : public OnMessageTask {
    tbb::task* execute() override;

public:
    SetImageChannelsTask(Session* session) : OnMessageTask(session) {}
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
    CARTA::SetHistogramRequirements _message;
    carta::EventHeader _header;

public:
    SetHistogramRequirementsTask(Session* session, CARTA::SetHistogramRequirements message, carta::EventHeader head)
        : OnMessageTask(session) {
        _message = message;
        _header = head;
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

class SetSpatialRequirementsTask : public OnMessageTask {
    tbb::task* execute() override;
    CARTA::SetSpatialRequirements _message;

public:
    SetSpatialRequirementsTask(Session* session, CARTA::SetSpatialRequirements message) : OnMessageTask(session) {
        _message = message;
    }
    ~SetSpatialRequirementsTask() = default;
};

class SetSpectralRequirementsTask : public OnMessageTask {
    tbb::task* execute() override;
    CARTA::SetSpectralRequirements _message;

public:
    SetSpectralRequirementsTask(Session* session, CARTA::SetSpectralRequirements message) : OnMessageTask(session) {
        _message = message;
    }
    ~SetSpectralRequirementsTask() = default;
};

class SetStatsRequirementsTask : public OnMessageTask {
    tbb::task* execute() override;
    CARTA::SetStatsRequirements _message;

public:
    SetStatsRequirementsTask(Session* session, CARTA::SetStatsRequirements message) : OnMessageTask(session) {
        _message = message;
    }
    ~SetStatsRequirementsTask() = default;
};

class SetRegionTask : public OnMessageTask {
    tbb::task* execute() override;
    CARTA::SetRegion _message;
    carta::EventHeader _header;

public:
    SetRegionTask(Session* session, CARTA::SetRegion message, carta::EventHeader head) : OnMessageTask(session) {
        _message = message;
        _header = head;
    }
    ~SetRegionTask() = default;
};

class RemoveRegionTask : public OnMessageTask {
    tbb::task* execute() override;
    CARTA::RemoveRegion _message;

public:
    RemoveRegionTask(Session* session, CARTA::RemoveRegion message) : OnMessageTask(session) {
        _message = message;
    }
    ~RemoveRegionTask() = default;
};

#endif // CARTA_BACKEND__ONMESSAGETASK_H_
