/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>
#include <omp.h>
#include <filesystem>

#include "CommonTestUtilities.h"
#include "Logger/Logger.h"
#include "Main/ProgramSettings.h"
#include "ThreadManager/ThreadManager.h"

#define TASK_THREAD_COUNT 3
namespace fs = std::filesystem;

using namespace carta;

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    testing::AddGlobalTestEnvironment(new CartaEnvironment());

    auto& settings = ProgramSettings::GetInstance();
    settings.no_log = true;
    settings.verbosity = 0;
    settings.log_performance = false;
    settings.log_protocol_messages = false;

    int omp_threads = omp_get_num_procs();
    ThreadManager::StartEventHandlingThreads(TASK_THREAD_COUNT);
    ThreadManager::SetThreadLimit(omp_threads);

    const char* home = getenv("HOME");
    if (home) {
        settings.user_directory = fs::path(home) / CARTA_USER_FOLDER_PREFIX;
    }

    logger::InitLogger();

    int result = RUN_ALL_TESTS();

    ThreadManager::ExitEventHandlingThreads();
    logger::FlushLogFile();

    return result;
}
