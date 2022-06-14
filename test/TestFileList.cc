/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021, 2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "CommonTestUtilities.h"
#include "FileList/FileListHandler.h"
#include "Util/Message.h"

using namespace carta;

class FileListTest : public ::testing::Test {
public:
    void TestFileList(const std::string& top_level_folder, const std::string& starting_folder, const CARTA::FileListRequest& request,
        bool expected_success = true) {
        std::shared_ptr<FileListHandler> file_list_handler = std::make_shared<FileListHandler>(top_level_folder, starting_folder);
        CARTA::FileListResponse response;
        FileListHandler::ResultMsg result_msg;
        file_list_handler->OnFileListRequest(request, response, result_msg);

        EXPECT_EQ(response.success(), expected_success);
        if (!response.success()) {
            return;
        }

        std::set<std::string> files = {"M17_SWex_unit.fits", "M17_SWex_unit.hdf5", "M17_SWex_unit.image", "M17_SWex_unit.miriad"};
        EXPECT_EQ(response.files_size(), files.size());
        if (response.files_size() == files.size()) {
            for (auto file : response.files()) {
                auto search = files.find(file.name());
                EXPECT_NE(search, files.end());
            }
        }

        std::set<std::string> subdirectories = {"empty.fits", "empty.hdf5", "empty.image", "empty.miriad", "empty_folder"};
        EXPECT_EQ(response.subdirectories_size(), subdirectories.size());
        if (response.subdirectories_size() == subdirectories.size()) {
            for (auto subdirectory : response.subdirectories()) {
                auto search = subdirectories.find(subdirectory.name());
                EXPECT_NE(search, subdirectories.end());
                if (search != subdirectories.end()) {
                    EXPECT_EQ(subdirectory.item_count(), 1);
                }
            }
        }
    }
};

TEST_F(FileListTest, SetTopLevelFolder) {
    std::string abs_path = (TestRoot() / "data" / "images" / "mix").string();

    auto request1 = Message::FileListRequest(abs_path);
    TestFileList("/", "", request1);
    TestFileList("", "", request1, false);

    auto request2 = Message::FileListRequest("data/images/mix");
    TestFileList(TestRoot().string(), "", request2);

    auto request3 = Message::FileListRequest("");
    TestFileList(abs_path, "", request3);

    auto request4 = Message::FileListRequest(".");
    TestFileList(abs_path, "", request4);
}

TEST_F(FileListTest, SetStartingFolder) {
    std::string abs_path = (TestRoot() / "data" / "images" / "mix").string();

    auto request1 = Message::FileListRequest("$BASE/data/images/mix");
    TestFileList("/", TestRoot().string(), request1);

    auto request2 = Message::FileListRequest("$BASE");
    TestFileList(TestRoot().string(), "data/images/mix", request2);
    TestFileList("/", abs_path, request2);
    TestFileList("", abs_path, request2, false);
}

TEST_F(FileListTest, AccessFalseFolder) {
    auto request = Message::FileListRequest("$BASE/folder_not_existed");
    TestFileList(TestRoot().string(), "data/images/mix", request, false);
}

TEST_F(FileListTest, AccessForbiddenFolder) {
    auto request1 = Message::FileListRequest("..");
    TestFileList(TestRoot().string(), "", request1, false);

    auto request2 = Message::FileListRequest("../../..");
    TestFileList(TestRoot().string(), "", request2, false);
}
