/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef __THREADING_H__
#define __THREADING_H__

#include <omp.h>

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

public:
    static void ApplyThreadLimit();
    static void SetThreadLimit(int count);
};
} // namespace carta

#endif // __THREADING_H__
