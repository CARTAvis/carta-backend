/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "Util/Casacore.h"

TEST(NormalizedUnitsTest, ValidBunits) {
    std::vector<casacore::String> input_bunits = {"Jy/beam", "mJy/beam", "MJy/beam"};

    for (auto bunit : input_bunits) {
        EXPECT_TRUE(casacore::UnitVal::check(bunit));
    }
}

TEST(NormalizedUnitsTest, Bunit) {
    casacore::String norm_bunit("Jy/beam");
    std::vector<casacore::String> input_bunits = {"Jy/beam", "Jy/Beam", "JY/Beam", "JY/BEAM", "jypb", "jy beam-1", "jy beam^-1"};

    for (auto bunit : input_bunits) {
        NormalizeUnit(bunit);
        EXPECT_EQ(bunit, norm_bunit);
    }
}

TEST(NormalizedUnitsTest, BunitPrefixUpperM) {
    casacore::String norm_bunit("MJy/beam");
    std::vector<casacore::String> input_bunits = {"MJy/beam", "MJy/Beam", "MJY/Beam", "MJY/BEAM", "Mjypb", "Mjy beam-1", "Mjy beam^-1"};

    for (auto bunit : input_bunits) {
        NormalizeUnit(bunit);
        EXPECT_EQ(bunit, norm_bunit);
    }
}

TEST(NormalizedUnitsTest, BunitPrefixLowerM) {
    casacore::String norm_bunit("mJy/beam");
    std::vector<casacore::String> input_bunits = {"mJy/beam", "mJy/Beam", "mJY/Beam", "mJY/BEAM", "mjypb", "mjy beam-1", "mjy beam^-1"};

    for (auto bunit : input_bunits) {
        NormalizeUnit(bunit);
        EXPECT_EQ(bunit, norm_bunit);
    }
}
