/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Stokes.h"

#include <casacore/casa/BasicSL/Constants.h>

int GetStokesValue(const CARTA::PolarizationType& stokes_type) {
    int stokes_value(-1);
    if (StokesValues.count(stokes_type)) {
        stokes_value = StokesValues[stokes_type];
    }
    return stokes_value;
}

CARTA::PolarizationType GetStokesType(int stokes_value) {
    CARTA::PolarizationType stokes_type = CARTA::PolarizationType::POLARIZATION_TYPE_NONE;
    if (StokesTypes.count(stokes_value)) {
        stokes_type = StokesTypes[stokes_value];
    }
    return stokes_type;
}

bool IsComputedStokes(int stokes) {
    return ((stokes >= COMPUTE_STOKES_PTOTAL) && (stokes <= COMPUTE_STOKES_PANGLE));
}

bool IsComputedStokes(const std::string& stokes) {
    return IsComputedStokes(StokesValues[StokesStringTypes[stokes]]);
}

float CalcPtotal(float val_q, float val_u, float val_v) {
    if (!std::isnan(val_q) && !std::isnan(val_u) && !std::isnan(val_v)) {
        return std::sqrt(std::pow(val_q, 2) + std::pow(val_u, 2) + std::pow(val_v, 2));
    }
    return FLOAT_NAN;
}

float CalcPlinear(float val_q, float val_u) {
    if (!std::isnan(val_q) && !std::isnan(val_u)) {
        return std::sqrt(std::pow(val_q, 2) + std::pow(val_u, 2));
    }
    return FLOAT_NAN;
}

float CalcPFtotal(float val_i, float val_q, float val_u, float val_v) {
    if (!std::isnan(val_i) && !std::isnan(val_q) && !std::isnan(val_u) && !std::isnan(val_v)) {
        return 100.0 * std::sqrt(std::pow(val_q, 2) + std::pow(val_u, 2) + std::pow(val_v, 2)) / val_i;
    }
    return FLOAT_NAN;
}

float CalcPFlinear(float val_i, float val_q, float val_u) {
    if (!std::isnan(val_i) && !std::isnan(val_q) && !std::isnan(val_u)) {
        return 100.0 * std::sqrt(std::pow(val_q, 2) + std::pow(val_u, 2)) / val_i;
    }
    return FLOAT_NAN;
}

float CalcPangle(float val_q, float val_u) {
    if (!std::isnan(val_q) && !std::isnan(val_u)) {
        return (180.0 / casacore::C::pi) * std::atan2((double)val_u, (double)val_q) / 2.0;
    }
    return FLOAT_NAN;
}