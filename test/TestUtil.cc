/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "SessionManager/ProgramSettings.h"
#include "Util.h"

using namespace std;

TEST(Util, SubdirectorySelf) {
    EXPECT_TRUE(IsSubdirectory("/", "/"));
}

TEST(Util, SubdirectorySimple) {
    EXPECT_TRUE(IsSubdirectory("/var", "/"));
    EXPECT_FALSE(IsSubdirectory("/", "/var"));
    EXPECT_TRUE(IsSubdirectory("/var/tmp", "/"));
    EXPECT_FALSE(IsSubdirectory("/", "/var/tmp"));
    EXPECT_TRUE(IsSubdirectory("/var/tmp", "/var"));
    EXPECT_FALSE(IsSubdirectory("/var", "/var/tmp"));
}