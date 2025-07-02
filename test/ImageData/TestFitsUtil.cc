/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "ImageData/FitsUtil.h"
#include "Util/Casacore.h"
#include "Util/String.h"

TEST(ParseHistoryBeamHeaderTest, ValidHistoryBeamFormat) {
    std::string header = "HISTORY RESTOR Beam = 2.000E+00 x 1.800E+00 arcsec, pa = 8.000E+01 degrees";
    std::string bmaj, bmin, bpa;

    EXPECT_TRUE(ParseHistoryBeamHeader(header, bmaj, bmin, bpa));
    EXPECT_EQ(bmaj, "2.000E+00arcsec");
    EXPECT_EQ(bmin, "1.800E+00arcsec");
    EXPECT_EQ(bpa, "8.000E+01deg");
}

TEST(ParseHistoryBeamHeaderTest, InvalidHistoryBeamFormat) {
    std::string header = "Invalid format";
    std::string bmaj, bmin, bpa;

    EXPECT_FALSE(ParseHistoryBeamHeader(header, bmaj, bmin, bpa));
}

TEST(ParseHistoryBeamHeaderTest, ValidGaussianBeam) {
    casacore::GaussianBeam beam(casacore::Quantity(1.5, "arcsec"), casacore::Quantity(1.2, "arcsec"), casacore::Quantity(45, "deg"));

    std::string formatted = FormatBeam(beam);
    EXPECT_NE(formatted.find("major: 1.500000 arcsec"), std::string::npos);
    EXPECT_NE(formatted.find("minor: 1.200000 arcsec"), std::string::npos);
    EXPECT_NE(formatted.find("pa: 45.000000 deg"), std::string::npos);
}
