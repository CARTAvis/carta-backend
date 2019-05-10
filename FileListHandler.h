// file list handler for all users' requests

#ifndef CARTA_BACKEND__FILELISTHANDLER_H_
#define CARTA_BACKEND__FILELISTHANDLER_H_

#include <unordered_map>

#include <fmt/format.h>
#include <tbb/mutex.h>

#include <casacore/casa/aips.h>
#include <casacore/images/Images/ImageOpener.h>

#include <carta-protobuf/file_list.pb.h>

#include "Util.h"

class FileListHandler {
public:
    FileListHandler(std::unordered_map<std::string, std::vector<std::string>>& permissions_map, bool enforce_permissions,
        const std::string& root, const std::string& base);

    struct ResultMsg {
        std::string message;
        std::vector<std::string> tags;
        CARTA::ErrorSeverity severity;
    };

    void OnFileListRequest(
        std::string api_key, const CARTA::FileListRequest& request, CARTA::FileListResponse& response, ResultMsg& result_msg);

private:
    // lock on file list handler
    tbb::mutex _file_list_mutex;
    // ICD: File list response
    void GetRelativePath(std::string& folder);
    void GetFileList(CARTA::FileListResponse& fileList, std::string folder, ResultMsg& result_msg);
    bool CheckPermissionForDirectory(std::string prefix);
    bool CheckPermissionForEntry(const std::string& entry);
    std::string GetType(casacore::ImageOpener::ImageTypes type); // convert enum to string
    bool FillFileInfo(CARTA::FileInfo* file_info, const std::string& filename);
    // permissions
    std::unordered_map<std::string, std::vector<std::string>>& _permissions_map;
    bool _permissions_enabled;
    bool _verbose_logging;
    std::string _api_key;
    std::string _root_folder, _base_folder, _filelist_folder;
};

#endif // CARTA_BACKEND__FILELISTHANDLER_H_
