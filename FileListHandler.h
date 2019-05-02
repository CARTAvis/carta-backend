// file list handler for all users' requests

#ifndef CARTA_BACKEND__FILELISTHANDLER_H_
#define CARTA_BACKEND__FILELISTHANDLER_H_

#include <unordered_map>

#include <fmt/format.h>
#include <tbb/mutex.h>

#include <casacore/casa/aips.h>
#include <casacore/images/Images/ImageOpener.h>

#include <carta-protobuf/file_list.pb.h>

#include "util.h"

class FileListHandler {
public:
    FileListHandler(std::unordered_map<std::string, std::vector<std::string>>& permissionsMap, bool enforcePermissions, std::string root,
        std::string base);
    ~FileListHandler();

    struct ResultMsg {
        std::string message;
        std::vector<std::string> tags;
        CARTA::ErrorSeverity severity;
    };

    void onFileListRequest(std::string api_key, const CARTA::FileListRequest& request, uint32_t requestId,
        CARTA::FileListResponse& response, ResultMsg& resultMsg);

private:
    // lock on file list handler
    tbb::mutex fileListMutex;
    // ICD: File list response
    void getRelativePath(std::string& folder);
    void getFileList(CARTA::FileListResponse& fileList, std::string folder, ResultMsg& resultMsg);
    bool checkPermissionForDirectory(std::string prefix);
    bool checkPermissionForEntry(std::string entry);
    std::string getType(casacore::ImageOpener::ImageTypes type); // convert enum to string
    bool fillFileInfo(CARTA::FileInfo* fileInfo, const std::string& filename);
    // permissions
    std::unordered_map<std::string, std::vector<std::string>>& permissionsMap;
    bool permissionsEnabled;
    bool verboseLogging;
    std::string apiKey;
    std::string rootFolder, baseFolder, filelistFolder;
};

#endif // CARTA_BACKEND__FILELISTHANDLER_H_
