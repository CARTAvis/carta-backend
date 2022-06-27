/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "Frame/Frame.h"

#include "MockFileLoader.h"

using namespace carta;

TEST(FrameTest, TestFileName) {
    auto loader = std::make_shared<MockFileLoader>();
    EXPECT_CALL(*loader, GetFileName()).Times(Exactly(1)).WillOnce(Return("test_name"));
    
    Frame frame(0, loader, 0);
    EXPECT_EQUALS(frame.GetFileName(), "test_name");
}
