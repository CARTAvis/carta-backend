/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
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

    void TestFileListSubdirectories(const std::string& top_level_folder, const std::string& starting_folder,
        const CARTA::FileListRequest& request, bool expected_success = true) {
        std::shared_ptr<FileListHandler> file_list_handler = std::make_shared<FileListHandler>(top_level_folder, starting_folder);
        CARTA::FileListResponse response;
        FileListHandler::ResultMsg result_msg;
        file_list_handler->OnFileListRequest(request, response, result_msg);

        EXPECT_EQ(response.success(), expected_success);
        if (!response.success()) {
            return;
        }
        // Expect no image files, non-zero subdirectories
        EXPECT_EQ(response.files_size(), 0);
        EXPECT_GT(response.subdirectories_size(), 0);
    }
};

TEST_F(FileListTest, SetTopLevelFolder) {
    std::string abs_path = (TestRoot() / "data" / "images" / "mix").string();
    std::cout << "[DEBUG] abs_path = " << abs_path << std::endl;

    for (const auto& entry : std::filesystem::directory_iterator(abs_path)) {
        std::cout << "[DEBUG] entry = " << entry.path().string() << std::endl;
    }

    auto request1 = Message::FileListRequest(abs_path);
    std::cout << "[DEBUG] Running TestFileList with / and request1" << std::endl;
    TestFileList("/", "", request1);
    std::cout << "[DEBUG] Running TestFileList with empty and request1, recursive = false" << std::endl;
    TestFileList("", "", request1, false);

    std::cout << "[DEBUG] Creating request2 with 'data/images/mix'" << std::endl;
    auto request2 = Message::FileListRequest("data/images/mix");
    std::cout << "[DEBUG] Running TestFileList with TestRoot and request2" << std::endl;
    TestFileList(TestRoot().string(), "", request2);

    std::cout << "[DEBUG] Creating request3 with empty string" << std::endl;
    auto request3 = Message::FileListRequest("");
    std::cout << "[DEBUG] Running TestFileList with abs_path and request3" << std::endl;
    TestFileList(abs_path, "", request3);

    std::cout << "[DEBUG] Creating request4 with '.'" << std::endl;
    auto request4 = Message::FileListRequest(".");
    std::cout << "[DEBUG] Running TestFileList with abs_path and request4" << std::endl;
    TestFileList(abs_path, "", request4);

    // Request file list for default top folder "/"
    // 0 image files, > 0 subdirectories
    std::cout << "[DEBUG] Creating request5 with empty string" << std::endl;
    auto request5 = Message::FileListRequest("");
    std::cout << "[DEBUG] Running TestFileListSubdirectories with / and request5" << std::endl;
    TestFileListSubdirectories("/", "", request5);
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
