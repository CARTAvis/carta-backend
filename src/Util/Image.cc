/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Image.h"

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

bool ComputedStokes(int stokes) {
    return ((stokes == COMPUTE_STOKES_PTOTAL) || (stokes == COMPUTE_STOKES_PLINEAR) || (stokes == COMPUTE_STOKES_PFTOTAL) ||
            (stokes == COMPUTE_STOKES_PFLINEAR) || (stokes == COMPUTE_STOKES_PANGLE));
}

bool ComputedStokes(const std::string& stokes_type) {
    return ((stokes_type == "Ptotal") || (stokes_type == "Plinear") || (stokes_type == "PFtotal") || (stokes_type == "PFlinear") ||
            (stokes_type == "Pangle"));
}
