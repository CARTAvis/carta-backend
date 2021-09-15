/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "File.h"

uint32_t GetMagicNumber(const string& filename) {
    uint32_t magic_number = 0;

    ifstream input_file(filename);
    if (input_file) {
        input_file.read((char*)&magic_number, sizeof(magic_number));
        input_file.close();
    }

    return magic_number;
}

bool IsCompressedFits(const std::string& filename) {
    // Check if gzip file, then check .fits extension
    bool is_fits(false);
    auto magic_number = GetMagicNumber(filename);
    if (magic_number == GZ_MAGIC_NUMBER) {
        fs::path gz_path(filename);
        is_fits = (gz_path.stem().extension().string() == ".fits");
    }

    return is_fits;
}

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
