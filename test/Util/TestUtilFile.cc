/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>
#include "Util/File.h"

#include "CommonTestUtilities.h"

class FileUtilTest : public ::testing::Test {
public:
    void SetUp() {
        working_directory = fs::current_path();
    }

    void TearDown() {
        fs::current_path(working_directory);
    }

private:
    fs::path working_directory;
};

TEST_F(FileUtilTest, ItemCountValidFolder) {
    auto pwd = TestRoot();
    EXPECT_EQ(GetNumItems((pwd / "data/tables").string()), 2);
    EXPECT_EQ(GetNumItems((pwd / "data/tables/xml").string()), 6);
}

TEST_F(FileUtilTest, ItemCountMissingFolder) {
    auto pwd = TestRoot();
    EXPECT_EQ(GetNumItems((pwd / "data/missing_folder").string()), -1);
}
