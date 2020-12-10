/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// file list handler for all users' requests

#ifndef CARTA_BACKEND__FILELISTHANDLER_H_
#define CARTA_BACKEND__FILELISTHANDLER_H_

#include <mutex>
#include <unordered_map>

#include <fmt/format.h>

#include <casacore/casa/aips.h>

#include <carta-protobuf/file_list.pb.h>
#include <carta-protobuf/region_file_info.pb.h>
#include <carta-protobuf/region_list.pb.h>

#include "../Util.h"

class FileListHandler {
public:
    FileListHandler(const std::string& root, const std::string& base);

    struct ResultMsg {
        std::string message;
        std::vector<std::string> tags;
        CARTA::ErrorSeverity severity;
    };

    void OnFileListRequest(const CARTA::FileListRequest& request, CARTA::FileListResponse& response, ResultMsg& result_msg);

    void OnRegionListRequest(
        const CARTA::RegionListRequest& region_request, CARTA::RegionListResponse& region_response, ResultMsg& result_msg);
    void OnRegionFileInfoRequest(
        const CARTA::RegionFileInfoRequest& request, CARTA::RegionFileInfoResponse& response, ResultMsg& result_msg);

private:
    // ICD: File/Region list response
    void GetFileList(CARTA::FileListResponse& file_list, std::string folder, ResultMsg& result_msg, bool region_list = false);

    bool FillFileInfo(CARTA::FileInfo& file_info, const std::string& filename);
    bool FillRegionFileInfo(CARTA::FileInfo& file_info, const string& filename, CARTA::FileType type = CARTA::FileType::UNKNOWN);
    void GetRegionFileContents(std::string& full_name, std::vector<std::string>& file_contents);

    void GetRelativePath(std::string& folder);
    std::string GetCasacoreTypeString(casacore::ImageOpener::ImageTypes type); // convert enum to string
    CARTA::FileType GetRegionType(const std::string& filename);                // parse first line for CRTF or DS9

    // lock on file list handler
    std::mutex _file_list_mutex;
    std::mutex _region_list_mutex;
    std::string _filelist_folder;
    std::string _regionlist_folder;

    bool _verbose_logging;
    bool _performance_logging;
    std::string _root_folder, _base_folder;
};

#endif // CARTA_BACKEND__FILELISTHANDLER_H_
