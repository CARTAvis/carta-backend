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
    Session* session;
    virtual tbb::task* execute() = 0;

public:
    OnMessageTask(Session* session_) {
        session = session_;
        session->increase_ref_count();
    }
    ~OnMessageTask() {
        if (!session->decrease_ref_count())
            delete session;
        session = 0;
    }
};

class MultiMessageTask : public OnMessageTask {
    CARTA::EventHeader _header;
    int _event_length;
    char* _event_buffer;
    tbb::task* execute();

public:
    MultiMessageTask(Session* session_, CARTA::EventHeader& head, int evt_len, char* event_buf) : OnMessageTask(session_) {
        _header = head;
        _event_length = evt_len;
        _event_buffer = event_buf;
    }
    ~MultiMessageTask() {}
};

class SetImageChannelsTask : public OnMessageTask {
    std::pair<CARTA::SetImageChannels, uint32_t> _req;
    tbb::task* execute();

public:
    SetImageChannelsTask(Session* session_, std::pair<CARTA::SetImageChannels, uint32_t> req_) : OnMessageTask(session_) {
        _req = req_;
    }
    ~SetImageChannelsTask() {}
};

class SetImageViewTask : public OnMessageTask {
    int _file_id;
    tbb::task* execute();

public:
    SetImageViewTask(Session* session_, int fd) : OnMessageTask(session_) {
        _file_id = fd;
    }
    ~SetImageViewTask() {}
};

class SetCursorTask : public OnMessageTask {
    int _file_id;
    tbb::task* execute();

public:
    SetCursorTask(Session* session, int fd) : OnMessageTask(session) {
        _file_id = fd;
    }
    ~SetCursorTask() {}
};

class SetHistogramReqsTask : public OnMessageTask {
    tbb::task* execute();
    CARTA::EventHeader _header;
    int _event_length;
    char* _event_buffer;

public:
    SetHistogramReqsTask(Session* session, CARTA::EventHeader& head, int len, char* buf) : OnMessageTask(session) {
        _header = head;
        _event_length = len;
        _event_buffer = buf;
    }
    ~SetHistogramReqsTask() {}
};

class AnimationTask : public OnMessageTask {
    std::tuple<uint8_t, uint32_t, std::vector<char>> _msg;
    tbb::task* execute();

public:
    AnimationTask(Session* session, uint32_t req_id, CARTA::StartAnimation msg) : OnMessageTask(session) {
        session->build_animation_object(msg, req_id);
    }
    ~AnimationTask() {}
};

#endif // CARTA_BACKEND__ONMESSAGETASK_H_
