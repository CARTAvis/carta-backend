/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2024 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_THREADINGMANAGER_THREADINGMANAGER_H_
#define CARTA_SRC_THREADINGMANAGER_THREADINGMANAGER_H_

#include <omp.h>
#include "Session/OnMessageTask.h"

#define MAX_TILING_TASKS 8

#if __has_include(<parallel/algorithm>)
#include <parallel/algorithm>
#define parallel_sort(...) __gnu_parallel::sort(__VA_ARGS__)
#elif __has_include(<execution>) && defined(_LIBCPP_HAS_PARALLEL_ALGORITHMS)
#include <execution>
#define parallel_sort(...) std::sort(std::execution::par_unseq, __VA_ARGS__)
#else
#define parallel_sort(...) std::sort(__VA_ARGS__)
#endif

namespace carta {
class ThreadManager {
    static int _omp_thread_count;
    static std::list<OnMessageTask*> _task_queue;
    static std::mutex _task_queue_mtx;
    static std::condition_variable _task_queue_cv;
    static std::list<std::thread*> _workers;
    static volatile bool _has_exited;

public:
    static void ApplyThreadLimit();
    static void SetThreadLimit(int count);
    static void StartEventHandlingThreads(int num_threads);
    static void QueueTask(OnMessageTask*);
    static void ExitEventHandlingThreads();
};

} // namespace carta

#endif // CARTA_SRC_THREADINGMANAGER_THREADINGMANAGER_H_
