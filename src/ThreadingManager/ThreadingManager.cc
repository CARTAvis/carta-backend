/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021, 2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ThreadingManager.h"

namespace carta {
int ThreadManager::_omp_thread_count = 0;
std::list<OnMessageTask*> ThreadManager::_task_queue;
std::mutex ThreadManager::_task_queue_mtx;
std::condition_variable ThreadManager::_task_queue_cv;
volatile bool ThreadManager::_has_exited = false;
std::list<std::thread*> ThreadManager::_workers;

void ThreadManager::ApplyThreadLimit() {
    // Skip application if we are already inside an OpenMP parallel block
    if (omp_get_num_threads() > 1) {
        return;
    }

    if (_omp_thread_count > 0) {
        omp_set_num_threads(_omp_thread_count);
    } else {
        omp_set_num_threads(omp_get_num_procs());
    }
}

void ThreadManager::SetThreadLimit(int count) {
    _omp_thread_count = count;
    ApplyThreadLimit();
}

void ThreadManager::QueueTask(OnMessageTask* tsk) {
    std::unique_lock<std::mutex> lock(_task_queue_mtx);
    _task_queue.push_back(tsk);
    _task_queue_cv.notify_one();
}

void ThreadManager::StartEventHandlingThreads(int num_threads) {
    auto thread_lambda = []() {
        OnMessageTask* tsk;

        do {
            std::unique_lock<std::mutex> lock(_task_queue_mtx);

            if (_task_queue.empty()) {
                _task_queue_cv.wait(lock);
            } else {
                tsk = _task_queue.front();
                _task_queue.pop_front();
                lock.unlock();
                tsk->execute();
                delete tsk;
            }

            if (_has_exited) {
                return;
            }
        } while (true);
    };

    // Start worker threads
    for (int i = 0; i < num_threads; i++) {
        _workers.push_back(new std::thread(thread_lambda));
    }
}

void ThreadManager::ExitEventHandlingThreads() {
    _has_exited = true;
    _task_queue_cv.notify_all();

    while (!_workers.empty()) {
        std::thread* thr = _workers.front();
        thr->join();
        _workers.pop_front();
        delete thr;
    }
}

} // namespace carta
