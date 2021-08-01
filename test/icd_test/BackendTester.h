/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_BACKENDTESTER_H_
#define CARTA_BACKEND_BACKENDTESTER_H_

#include <memory>

#include "DummyBackend.h"

class BackendTester {
public:
    BackendTester();
    virtual ~BackendTester() = default;

protected:
    std::unique_ptr<DummyBackend> _dummy_backend;
};

#endif // CARTA_BACKEND_BACKENDTESTER_H_
