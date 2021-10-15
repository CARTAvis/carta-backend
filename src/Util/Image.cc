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

bool GetCasaStokesType(const CARTA::PolarizationType& in_stokes_type, casacore::Stokes::StokesTypes& out_stokes_type) {
    if (CasaStokesTypes.count(in_stokes_type)) {
        out_stokes_type = CasaStokesTypes[in_stokes_type];
        return true;
    }
    return false;
}

// convert between CARTA::PolarizationType values and FITS standard stokes values
bool ConvertFitsStokesValue(const int& in_stokes_value, int& out_stokes_value) {
    if (in_stokes_value >= 1 && in_stokes_value <= 4) {
        out_stokes_value = in_stokes_value;
        return true;
    } else if ((in_stokes_value >= 4 && in_stokes_value <= 12) || (in_stokes_value <= -1 && in_stokes_value >= -8)) {
        // convert between [5, 6, ..., 12] and [-1, -2, ..., -8]
        out_stokes_value = -in_stokes_value + 4;
        return true;
    }
    return false;
}
