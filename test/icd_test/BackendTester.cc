/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "BackendTester.h"

ElapsedTimer::ElapsedTimer() {}

void ElapsedTimer::Start() {
    _t_start = std::chrono::high_resolution_clock::now();
}

int ElapsedTimer::Elapsed() {
    auto t_end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(t_end - _t_start).count();
}

BackendTester::BackendTester() : _dummy_backend(std::make_unique<DummyBackend>()) {}
