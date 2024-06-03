/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "InitialValueCalculator.h"

using namespace carta;

InitialValueCalculator::InitialValueCalculator(float* image) {
    _image = image;
}

bool InitialValueCalculator::CalculateInitialValues(std::vector<CARTA::GaussianComponent>& initial_values) {
    // TODO: generate intial values
    spdlog::info("generate initial values");
    return false;
}
