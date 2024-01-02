/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
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

template <typename T>
void CmpVectors(const std::vector<T>& data1, const std::vector<T>& data2, T abs_err) {
    EXPECT_EQ(data1.size(), data2.size());
    if (data1.size() == data2.size()) {
        for (int i = 0; i < data1.size(); ++i) {
            CmpValues(data1[i], data2[i], abs_err);
        }
    }
}

template <typename T>
void CmpValues(T data1, T data2, T abs_err) {
    if (!std::isnan(data1) || !std::isnan(data2)) {
        EXPECT_NEAR(data1, data2, abs_err);
    }
}

#endif // CARTA_TEST_COMMONTESTUTILITIES_TCC_
