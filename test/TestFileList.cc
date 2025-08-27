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
    CARTA::FileListResponse RequestFileList(
        const std::string& top_level_folder, const std::string& starting_folder, const CARTA::FileListRequest& request) {
        std::shared_ptr<FileListHandler> file_list_handler = std::make_shared<FileListHandler>(top_level_folder, starting_folder);
        CARTA::FileListResponse response;
        FileListHandler::ResultMsg result_msg;
        file_list_handler->OnFileListRequest(request, response, result_msg);
        return response;
    }

    void TestFileList(const std::string& top_level_folder, const std::string& starting_folder, const CARTA::FileListRequest& request,
        bool expected_success = true) {
        auto response = RequestFileList(top_level_folder, starting_folder, request);
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
        // Expect non-zero subdirectories
        EXPECT_GT(response.subdirectories_size(), 0);
    }

    void TestFileListResponse(
        const CARTA::FileListResponse& response, size_t expected_files, size_t expected_dirs, bool expect_file_type = true) {
        EXPECT_TRUE(response.success());
        EXPECT_EQ(response.files_size(), expected_files);
        EXPECT_EQ(response.subdirectories_size(), expected_dirs);

        for (size_t i = 0; i < response.files_size(); ++i) {
            TestFileInfo(response.files(i), expect_file_type);
        }

        for (size_t i = 0; i < response.subdirectories_size(); ++i) {
            TestDirectoryInfo(response.subdirectories(i), expect_file_type);
        }
    }

    void TestFileInfo(const CARTA::FileInfo& file_info, bool expect_file_type) {
        // Test contents of FileInfo submessage
        // Name, size, date should be set
        EXPECT_GT(file_info.name().size(), 0);
        if (file_info.name().find("empty") == std::string::npos) {
            EXPECT_GT(file_info.size(), 0);
        } else {
            // Empty file has size 0
            EXPECT_EQ(file_info.size(), 0);
        }
        EXPECT_GT(file_info.date(), 0);

        // Type set only if filter not AllFiles
        if (!expect_file_type) {
            EXPECT_EQ(file_info.type(), CARTA::FileType::UNKNOWN);
        }
    }

    void TestDirectoryInfo(const CARTA::DirectoryInfo& dir_info, bool expect_file_type) {
        // Test contents of DirectoryInfo submessage
        // Name, date should always be set
        EXPECT_GT(dir_info.name().size(), 0);
        EXPECT_GT(dir_info.date(), 0);

        // Item count set only if know type
        if (expect_file_type) {
            EXPECT_GT(dir_info.item_count(), 0);
        } else {
            EXPECT_EQ(dir_info.item_count(), 0);
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

    // Request file list for default top folder "/"
    // 0 image files, > 0 subdirectories
    auto request5 = Message::FileListRequest("");
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

TEST_F(FileListTest, TestFilterModes) {
    // Test dir has 14 items:
    // - 4 images (2 dir and 2 file)
    // - 4 empty files with image extensions
    // - 4 empty directories with image extensions
    // - 1 empty directory
    // - 1 txt file
    // Files and subdirectories in response are in random order.

    // Filter mode Content
    // Empty dirs are dirs, ignores empty files and txt file.
    auto request1 = Message::FileListRequest("data/images/mix");
    auto response = RequestFileList(TestRoot().string(), "", request1);
    TestFileListResponse(response, 4, 5);

    // Filter mode Extension
    // Empty fits/hdf5 files are images, empty dirs are dirs, ignores other empty files and txt.
    auto request2 = Message::FileListRequest("data/images/mix", CARTA::FileListFilterMode::Extension);
    response = RequestFileList(TestRoot().string(), "", request2);
    TestFileListResponse(response, 6, 5);

    // Filter mode AllFiles
    // All are files or dirs
    auto request3 = Message::FileListRequest("data/images/mix", CARTA::FileListFilterMode::AllFiles);
    response = RequestFileList(TestRoot().string(), "", request3);
    TestFileListResponse(response, 7, 7, false);

    // Filter mode AllFiles with image as directory should have one FileInfo for the image
    auto request4 = Message::FileListRequest("data/images/mix/M17_SWex_unit.image", CARTA::FileListFilterMode::AllFiles);
    response = RequestFileList(().string(), "", request4);
    TestFileListResponse(response, 1, 0);
}
