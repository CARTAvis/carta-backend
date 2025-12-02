/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>
#include <unistd.h>
#include "Util/Memory.h"

class MemoryUtilTest : public ::testing::Test {};

TEST_F(MemoryUtilTest, TestPageAlignedFloatArray) {
    auto data = MakeUniqueAlignedDataPtr<float>(10);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(data.get()) % sysconf(_SC_PAGE_SIZE), 0);
}
