/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/
#include <unordered_set>

#include <gtest/gtest.h>

#include "ImageData/Hdf5Attributes.h"
#include "src/Frame/Frame.h"

#include "CommonTestUtilities.h"

using namespace carta;

class Hdf5AttributesTest : public ::testing::Test, public ImageGenerator {};

TEST_F(Hdf5AttributesTest, TestAttributes) {
    auto padded = [](std::string s) { return fmt::format("{:<80}", s); };

    auto path_string = GeneratedHdf5ImagePath(fmt::format("10 10 -H '{}'", padded("BSCALE  = 1.0")));
    Hdf5DataReader reader(path_string);

    casacore::Vector<casacore::String> attributes;
    Hdf5Attributes::ReadAttributes(reader.GroupId(), attributes);

    EXPECT_EQ(attributes.size(), 11);
    EXPECT_EQ((std::string)attributes[0], padded("SCHEMA_VERSION= '0.3'"));
    EXPECT_EQ((std::string)attributes[1], padded("HDF5_CONVERTER= 'fits2idia'"));
    // We don't test the version because we don't build the converter ourselves
    EXPECT_EQ((std::string)attributes[3], padded("SIMPLE  = T"));
    EXPECT_EQ((std::string)attributes[4], padded("BITPIX  = -32"));
    EXPECT_EQ((std::string)attributes[5], padded("NAXIS   = 2"));
    EXPECT_EQ((std::string)attributes[6], padded("NAXIS1  = 10"));
    EXPECT_EQ((std::string)attributes[7], padded("NAXIS2  = 10"));
    EXPECT_EQ((std::string)attributes[8], padded("EXTEND  = T"));
    EXPECT_EQ((std::string)attributes[9], padded("BSCALE  = 1.000000000000"));
    EXPECT_EQ((std::string)attributes[10], "END");
}
