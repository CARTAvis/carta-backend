/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <benchmark/benchmark.h>
#include <tbb/task_scheduler_init.h>

#include <cstdlib>

#include "BackendTester.h"

// Modify BENCHMARK_MAIN() and set environment
int main(int argc, char** argv) {
    // Set spdlog verbosity level
    string keyword("verbosity=");
    string verbosity;
    for (int i = 0; i < argc; ++i) {
        string tmp = argv[i];
        size_t found = tmp.find(keyword);
        if (found != std::string::npos) {
            verbosity = tmp.substr(found + keyword.size(), 1);
            break;
        }
    }

    if (verbosity == "1") {
        spdlog::default_logger()->set_level(spdlog::level::critical);
    } else if (verbosity == "2") {
        spdlog::default_logger()->set_level(spdlog::level::err);
    } else if (verbosity == "3") {
        spdlog::default_logger()->set_level(spdlog::level::warn);
    } else if (verbosity == "4") {
        spdlog::default_logger()->set_level(spdlog::level::info);
    } else if (verbosity == "5") {
        spdlog::default_logger()->set_level(spdlog::level::debug);
    } else {
        spdlog::default_logger()->set_level(spdlog::level::off);
    }

    // Set TBB task threads
    tbb::task_scheduler_init task_scheduler(TBB_TASK_THREAD_COUNT);

    // Set OMP threads
    keyword = "omp_threads=";
    int opm_threads = omp_get_num_procs();

    for (int i = 0; i < argc; ++i) {
        string tmp = argv[i];
        size_t found = tmp.find(keyword);
        if (found != std::string::npos) {
            int tmp_opm_threads = atoi(tmp.substr(found + keyword.size(), 1).c_str());
            if (tmp_opm_threads != 0) {
                opm_threads = tmp_opm_threads;
            }
        }
    }
    omp_set_num_threads(opm_threads);
    string message = fmt::format("Set TBB task threads: {}, OMP threads: {}.", TBB_TASK_THREAD_COUNT, opm_threads);
    cout << message << endl;

    ::benchmark::Initialize(&argc, argv);
    ::benchmark::RunSpecifiedBenchmarks();
    ::benchmark::Shutdown();
    return 0;
}