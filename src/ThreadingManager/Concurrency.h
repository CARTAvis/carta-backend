/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021, 2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <condition_variable>
#include <iostream>
#include <list>
#include <mutex>
#include <shared_mutex>

namespace carta {
/*
  Thread-safe queue class.
 */
template <class T>
class concurrent_queue {
public:
    concurrent_queue<T>() {}

    ~concurrent_queue<T>() {
        clear();
    }

    void push(T elt) {
        _mtx.lock();
        _q.push_back(elt);
        _mtx.unlock();
    }

    bool try_pop(T& elt) {
        bool ret = false;
        _mtx.lock();
        if (!_q.empty()) {
            ret = true;
            elt = _q.front();
            _q.pop_front();
        }
        _mtx.unlock();
        return ret;
    }

    void clear() {
        _mtx.lock();
        while (!_q.empty()) {
            _q.pop_front();
        }
        _mtx.unlock();
    }

private:
    std::list<T> _q;
    std::mutex _mtx;
};

/*
  Mutex that allows many readers to enter a critical section, but only
  one writer at a time. Writers are queued so that writes happen in the
  order that they first attempt to enter a critical section.
 */
class queuing_rw_mutex {
public:
    queuing_rw_mutex() {
        _reader_count = 0;
        _writer_count = 0;
    }

    ~queuing_rw_mutex() {
        while (_writer_count-- > 0) {
            dequeue_one_writer();
        }
        _readers_cv.notify_all();
    }

    void reader_enter() {
        std::unique_lock<std::mutex> lock(_mtx);
        if (_writer_count > 0) {
            _readers_cv.wait(lock);
        }
        ++_reader_count;
    }

    void writer_enter() {
        std::unique_lock<std::mutex> lock(_mtx);
        ++_writer_count;
        if ((_reader_count > 0) || (_writer_count > 1)) {
            queue_writer(lock);
        }
    }

    void reader_leave() {
        std::unique_lock<std::mutex> lock(_mtx);

        --_reader_count;

        if ((_reader_count == 0) && (_writer_count > 0)) {
            dequeue_one_writer();
        }
    }

    void writer_leave() {
        std::unique_lock<std::mutex> lock(_mtx);
        --_writer_count;

        if (_writer_count > 0) {
            dequeue_one_writer();
        } else {
            _readers_cv.notify_all();
        }
    }

private:
    std::mutex _mtx;
    std::condition_variable _readers_cv;
    std::list<std::condition_variable*> _writers_cv_list;
    short _reader_count;
    short _writer_count;

    void queue_writer(std::unique_lock<std::mutex>& mtx) {
        std::condition_variable* cv = new std::condition_variable;
        _writers_cv_list.push_front(cv);
        cv->wait(mtx);
        delete cv;
    }

    void dequeue_one_writer() {
        if (!_writers_cv_list.empty()) {
            std::condition_variable* cv = _writers_cv_list.back();
            cv->notify_one();
            _writers_cv_list.pop_back();
        }
    }
};

/*
  Class to create objects that allow scoped access to critical
  sections that are protected by queuing_rw_mutex objects, with
  the critical section starting when one of these objects is
  created and ending either when the object goes out of scope,
  causing its destructor to be called, or when the release
  method is called on the object.
 */
class queuing_rw_mutex_scoped {
public:
    queuing_rw_mutex_scoped(queuing_rw_mutex* rwmtx, bool rw) {
        _rwmtx = rwmtx;
        _rw = rw;
        _active = true;
        if (rw) { // This is a writer
            rwmtx->writer_enter();
        } else { // this is a reader
            rwmtx->reader_enter();
        }
    }

    ~queuing_rw_mutex_scoped() {
        release();
    }

    void release() {
        if (_active) {
            if (_rw) {
                _rwmtx->writer_leave();
            } else {
                _rwmtx->reader_leave();
            }
            _active = false;
        }
    }

private:
    queuing_rw_mutex* _rwmtx;
    bool _rw;
    bool _active;
};

} // namespace carta
