/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2024 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// file list handler for all users' requests

#ifndef CARTA_SRC_FILELIST_FILELISTHANDLER_H_
#define CARTA_SRC_FILELIST_FILELISTHANDLER_H_

#include <functional>
#include <mutex>
#include <unordered_map>

#include <casacore/casa/aips.h>

#include <carta-protobuf/file_list.pb.h>
#include <carta-protobuf/region_file_info.pb.h>
#include <carta-protobuf/region_list.pb.h>

namespace carta {

class FileListHandler {
public:
    FileListHandler(const std::string& top_level_folder, const std::string& starting_folder);

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

    void StopGettingFileList() {
        _stop_getting_file_list = true;
    }
    void SetProgressCallback(const std::function<void(CARTA::ListProgress)>& progress_callback) {
        _progress_callback = progress_callback;
    }

private:
    // ICD: File/Region list response
    void GetFileList(CARTA::FileListResponse& file_list, const std::string& folder, ResultMsg& result_msg,
        CARTA::FileListFilterMode filter_mode, bool region_list = false);

    bool FillRegionFileInfo(CARTA::FileInfo& file_info, const std::string& filename, CARTA::FileType type = CARTA::FileType::UNKNOWN,
        bool determine_file_type = true);
    void GetRegionFileContents(std::string& full_name, std::vector<std::string>& file_contents);
    void GetRelativePath(std::string& folder);

    // lock on file list handler
    std::mutex _file_list_mutex;
    std::mutex _region_list_mutex;
    std::string _filelist_folder;
    std::string _regionlist_folder;
    std::string _top_level_folder, _starting_folder;

    volatile bool _stop_getting_file_list;
    volatile bool _first_report_made;
    std::function<void(CARTA::ListProgress)> _progress_callback;
};

} // namespace carta

#endif // CARTA_SRC_FILELIST_FILELISTHANDLER_H_
