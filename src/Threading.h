/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef __THREADING_H__
#define __THREADING_H__

#include <omp.h>

#define MAX_TILING_TASKS 8

namespace carta {
static int global_thread_count;

static void ApplyThreadLimit() {
    if (global_thread_count > 0) {
        omp_set_num_threads(carta::global_thread_count);
    }
}

static void SetThreadLimit(int count) {
    global_thread_count = count;
    ApplyThreadLimit();
}

} // namespace carta

#endif // __THREADING_H__
