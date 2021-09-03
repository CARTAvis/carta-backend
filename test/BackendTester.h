/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_ICD_TEST_BACKENDTESTER_H_
#define CARTA_BACKEND_ICD_TEST_BACKENDTESTER_H_

#include <memory>

#include <gtest/gtest.h>

#include "BackendModel.h"
#include "CommonTestUtilities.h"
#include "ProtobufUtilities.h"

class ElapsedTimer {
public:
    ElapsedTimer();
    ~ElapsedTimer() = default;

    void Start();
    int Elapsed(); // milli seconds elapsed

private:
    std::chrono::high_resolution_clock::time_point _t_start;
};

class BackendTester : public ::testing::Test, public FileFinder {
public:
    BackendTester();
    virtual ~BackendTester() = default;

protected:
    std::unique_ptr<BackendModel> _dummy_backend;
};

#endif // CARTA_BACKEND_ICD_TEST_BACKENDTESTER_H_
