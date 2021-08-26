/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "BackendTester.h"

bool FileExists(string filename) {
    fs::path fs_path_filename(filename);
    if (!fs::exists(fs_path_filename)) {
        spdlog::warn("File {} does not exist. Ignore the test.", filename);
        return false;
    }
    return true;
}

ElapsedTimer::ElapsedTimer() {}

void ElapsedTimer::Start() {
    _t_start = std::chrono::high_resolution_clock::now();
}

int ElapsedTimer::Elapsed() {
    auto t_end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(t_end - _t_start).count();
}

BackendTester::BackendTester() {
    uint32_t session_id(0);
    std::string address;
    std::string top_level_folder("/");
    std::string starting_folder("data/images");
    int grpc_port(-1);
    bool read_only_mode(false);

    _dummy_backend =
        std::make_unique<BackendModel>(nullptr, nullptr, session_id, address, top_level_folder, starting_folder, grpc_port, read_only_mode);
}
