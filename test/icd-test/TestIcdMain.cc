/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>
#include "BackendTester.h"
#include "CommonTestUtilities.h"

int main(int argc, char** argv) {
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

    testing::InitGoogleTest(&argc, argv);
    testing::AddGlobalTestEnvironment(new CartaEnvironment());
    return RUN_ALL_TESTS();
}
