/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2024 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_TEST_COMMONTESTUTILITIES_TCC_
#define CARTA_TEST_COMMONTESTUTILITIES_TCC_

template <typename T>
std::vector<T> GetSpectralProfileValues(const CARTA::SpectralProfile& profile) {
    std::string buffer;
    if constexpr (std::is_same_v<T, float>) {
        buffer = profile.raw_values_fp32();
    } else {
        buffer = profile.raw_values_fp64();
    }
    std::vector<T> values(buffer.size() / sizeof(T));
    memcpy(values.data(), buffer.data(), buffer.size());
    return values;
}

#endif // CARTA_TEST_COMMONTESTUTILITIES_TCC_
