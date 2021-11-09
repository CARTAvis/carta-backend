/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <iostream>
#include <list>
#include <mutex>
#include <shared_mutex>

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

class queuing_rw_mutex {
public:
    queuing_rw_mutex() {
        _reader_count = 0;
        _writer_count = 0;
    }
    ~queuing_rw_mutex() {
        while (!_mtx_list.empty()) {
            delete _mtx_list.front();
            _mtx_list.pop_front();
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
        if ((_reader_count > 0) || (_writer_count > 0)) {
            // Queue this write ready for when previous threads are complete.
            std::mutex* wait_mtx = new std::mutex();
            _mtx_list.push_front(wait_mtx);
            wait_mtx->lock();
        }
        ++_writer_count;
    }
    void reader_leave() {
        std::unique_lock<std::mutex> lock(_mtx);
        --_reader_count;
        if ((_reader_count == 0) && (_writer_count > 0)) {
            if (!_mtx_list.empty()) {
                std::mutex* wait_mtx = _mtx_list.back();
                _mtx_list.pop_back();
                wait_mtx->unlock();
                delete wait_mtx;
            }
        }
    }
    void writer_leave() {
        std::unique_lock<std::mutex> lock(_mtx);
        --_writer_count;
        if (!_mtx_list.empty()) {
            std::mutex* wait_mtx = _mtx_list.back();
            _mtx_list.pop_back();
            wait_mtx->unlock();
            delete wait_mtx;
        } else if (_reader_count > 0) {
            _readers_cv.notify_all();
        }
    }

private:
    std::list<std::mutex*> _mtx_list;
    std::mutex _mtx;
    std::condition_variable _readers_cv;
    short _reader_count;
    short _writer_count;
};

class queuing_rw_mutex_local {
public:
    queuing_rw_mutex_local(queuing_rw_mutex* rwmtx, bool rw) {
        _rwmtx = rwmtx;
        _rw = rw;
        _active = true;
        if (rw) { // This is a writer
            rwmtx->writer_enter();
        } else { // this is a reader
            rwmtx->reader_enter();
        }
    }
    ~queuing_rw_mutex_local() {
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
