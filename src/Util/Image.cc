/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Image.h"

int GetStokesValue(const CARTA::StokesType& stokes_type) {
    int stokes_value(-1);
    switch (stokes_type) {
        case CARTA::StokesType::I:
            stokes_value = 1;
            break;
        case CARTA::StokesType::Q:
            stokes_value = 2;
            break;
        case CARTA::StokesType::U:
            stokes_value = 3;
            break;
        case CARTA::StokesType::V:
            stokes_value = 4;
            break;
        default:
            break;
    }
    return stokes_value;
}

CARTA::StokesType GetStokesType(int stokes_value) {
    CARTA::StokesType stokes_type = CARTA::StokesType::STOKES_TYPE_NONE;
    switch (stokes_value) {
        case 1:
            stokes_type = CARTA::StokesType::I;
            break;
        case 2:
            stokes_type = CARTA::StokesType::Q;
            break;
        case 3:
            stokes_type = CARTA::StokesType::U;
            break;
        case 4:
            stokes_type = CARTA::StokesType::V;
            break;
        default:
            break;
    }
    return stokes_type;
}
