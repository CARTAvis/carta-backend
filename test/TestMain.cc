/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <string>

#include <gtest/gtest.h>
#include <omp.h>
#include <cxxopts/cxxopts.hpp>

#include "CommonTestUtilities.h"
#include "Logger/Logger.h"
#include "ThreadingManager/ThreadingManager.h"

#define TASK_THREAD_COUNT 3

int main(int argc, char** argv) {
    // Set gtest environment
    testing::InitGoogleTest(&argc, argv);
    testing::AddGlobalTestEnvironment(new CartaEnvironment());

    // Set global environment
    int verbosity(0);
    bool no_log(true);
    bool log_performance(false);
    bool log_protocol_messages(false);
    int omp_threads(omp_get_num_procs());

    cxxopts::Options options("carta-icd-test", "CARTA ICD test");

    // clang-format off
    options.add_options()
        ("h,help", "print usage")
        ("verbosity", "display verbose logging from this level",
         cxxopts::value<int>()->default_value(std::to_string(verbosity)), "<level>")
        ("no_log", "do not log output to a log file", cxxopts::value<bool>()->default_value("true"))
        ("log_performance", "enable performance debug logs", cxxopts::value<bool>()->default_value("false"))
        ("log_protocol_messages", "enable protocol message debug logs", cxxopts::value<bool>()->default_value("false"))
        ("t,omp_threads", "manually set OpenMP thread pool count",
         cxxopts::value<int>()->default_value(std::to_string(omp_threads)), "<threads>");
    // clang-format on

    auto result = options.parse(argc, argv);

    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        return 0;
    }

    verbosity = result["verbosity"].as<int>();
    no_log = result["no_log"].as<bool>();
    log_performance = result["log_performance"].as<bool>();
    log_protocol_messages = result["log_protocol_messages"].as<bool>();
    omp_threads = result["omp_threads"].as<int>();

    if (omp_threads < 0) {
        omp_threads = omp_get_num_procs();
    }

    carta::ThreadManager::StartEventHandlingThreads(TASK_THREAD_COUNT);
    carta::ThreadManager::SetThreadLimit(omp_threads);

    fs::path user_directory = fs::path(getenv("HOME")) / CARTA_USER_FOLDER_PREFIX;
    carta::logger::InitLogger(no_log, verbosity, log_performance, log_protocol_messages, user_directory);

    int run_all_tests = RUN_ALL_TESTS();

    ThreadManager::ExitEventHandlingThreads();
    carta::logger::FlushLogFile();
    return run_all_tests;
}
