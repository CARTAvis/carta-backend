/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "InitTester.h"
#include "CommonTestUtilities.h"
#include "Logger/Logger.h"

#include <string>

#include <omp.h>

using namespace std;

void InitOmpThreads(char* omp_threads) {
    string omp_threads_str(omp_threads);
    int omp_thread_count = stoi(omp_threads_str);
    if (omp_thread_count > 0) {
        omp_set_num_threads(omp_thread_count);
        spdlog::debug("OMP threads {}", omp_thread_count);
    } else {
        omp_set_num_threads(omp_get_num_procs());
        spdlog::debug("OMP threads {}", omp_get_num_procs());
    }
}

void InitSpdlog(char* verbosity) {
    string verbosity_str(verbosity);
    fs::path user_directory("");
    int verb = stoi(verbosity_str);
    carta::logger::InitLogger(true, verb, true, false, user_directory);
}
