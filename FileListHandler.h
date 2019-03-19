// file list handler

#pragma once

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <unordered_map>

#include <casacore/casa/aips.h>
#include <casacore/casa/OS/File.h>
#include <fmt/format.h>
#include <tbb/concurrent_queue.h>
#include <tbb/mutex.h>

#include <carta-protobuf/file_info.pb.h>
#include <carta-protobuf/file_list.pb.h>

#include "Frame.h"

class FileListHandler {
public:
    FileListHandler(std::unordered_map<std::string, std::vector<std::string>>& permissionsMap,
                    bool enforcePermissions,
                    std::string root,
                    std::string base);
    ~FileListHandler();

    struct ResultMsg {
        std::string message;
        std::vector<std::string> tags;
        CARTA::ErrorSeverity severity;
    };

    void onFileListRequest(const CARTA::FileListRequest& request,
                           uint32_t requestId,
                           CARTA::FileListResponse &response,
                           ResultMsg& resultMsg);
private:
    // lock on file list handler
    tbb::mutex fileListMutex;
    // CARTA ICD
    void getRelativePath(std::string& folder);
    void getFileList(CARTA::FileListResponse& fileList, std::string folder, ResultMsg& resultMsg);
    bool checkPermissionForDirectory(std:: string prefix);
    bool checkPermissionForEntry(std::string entry);
    std::string getType(casacore::ImageOpener::ImageTypes type); // convert enum to string
    bool fillFileInfo(CARTA::FileInfo* fileInfo, const std::string& filename);
    // permissions
    std::unordered_map<std::string, std::vector<std::string> >& permissionsMap;
    bool permissionsEnabled;
    bool verboseLogging;
    std::string apiKey;
    std::string rootFolder, baseFolder, filelistFolder;
};
