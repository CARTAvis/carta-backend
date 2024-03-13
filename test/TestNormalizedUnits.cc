/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "Util/Casacore.h"

std::vector<casacore::String> TEST_BUNITS = {"Jy/beam", "Jy/Beam", "JY/Beam", "JY/BEAM", "jypb", "Jypb", "JYpb", "jy beam-1", "jy beam^-1",
    "Jy beam-1", "Jy beam^-1", "JY beam-1", "JY beam^-1", "jy Beam-1", "jy Beam^-1", "Jy Beam-1", "Jy Beam^-1", "JY Beam-1", "JY Beam^-1",
    "beam-1 jy", "beam^-1 jy", "beam-1 Jy", "beam^-1 Jy", "beam-1 JY", "beam^-1 JY", "Beam-1 jy", "jy Beam^-1", "Beam-1 Jy", "Beam^-1 Jy",
    "Beam-1 JY", "Beam^-1 JY"};

TEST(NormalizedUnitsTest, ValidBunits) {
    std::vector<casacore::String> valid_bunits = {"Jy/beam", "mJy/beam", "MJy/beam"};
    for (auto bunit : valid_bunits) {
        EXPECT_TRUE(casacore::UnitVal::check(bunit));
    }

    std::vector<casacore::String> invalid_bunits = {"Jy/Beam", "\"jy/beam\"", "counts/s", "MYJy/beam"};
    for (auto bunit : invalid_bunits) {
        EXPECT_FALSE(casacore::UnitVal::check(bunit));
    }
}

TEST(NormalizedUnitsTest, Bunit) {
    casacore::String norm_bunit("Jy/beam");
    for (auto bunit : TEST_BUNITS) {
        NormalizeUnit(bunit);
        EXPECT_EQ(bunit, norm_bunit);
    }
}

TEST(NormalizedUnitsTest, BunitPrefixUpperM) {
    casacore::String norm_bunit("MJy/beam");
    for (auto bunit : TEST_BUNITS) {
        bunit = "M" + bunit;
        NormalizeUnit(bunit);
        EXPECT_EQ(bunit, norm_bunit);
    }
}

TEST(NormalizedUnitsTest, BunitPrefixLowerM) {
    casacore::String norm_bunit("mJy/beam");
    for (auto bunit : TEST_BUNITS) {
        bunit = "m" + bunit;
        NormalizeUnit(bunit);
        EXPECT_EQ(bunit, norm_bunit);
    }
}
