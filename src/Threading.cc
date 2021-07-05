/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Threading.h"

namespace carta {
int ThreadManager::_omp_thread_count = 0;

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
} // namespace carta
