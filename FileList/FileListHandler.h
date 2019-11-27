// file list handler for all users' requests

#ifndef CARTA_BACKEND__FILELISTHANDLER_H_
#define CARTA_BACKEND__FILELISTHANDLER_H_

#include <unordered_map>

#include <fmt/format.h>
#include <tbb/mutex.h>

#include <casacore/casa/aips.h>

#include <carta-protobuf/file_list.pb.h>
#include <carta-protobuf/region_file_info.pb.h>
#include <carta-protobuf/region_list.pb.h>

#include "../Util.h"

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

    void OnRegionListRequest(
        const CARTA::RegionListRequest& region_request, CARTA::RegionListResponse& region_response, ResultMsg& result_msg);
    void OnRegionFileInfoRequest(
        const CARTA::RegionFileInfoRequest& request, CARTA::RegionFileInfoResponse& response, ResultMsg& result_msg);

private:
    // ICD: File/Region list response
    void GetFileList(CARTA::FileListResponse& file_list, std::string folder, ResultMsg& result_msg, bool region_list = false);

    bool FillFileInfo(CARTA::FileInfo* file_info, const std::string& filename);
    bool FillRegionFileInfo(CARTA::FileInfo* file_info, const string& filename, CARTA::FileType type = CARTA::FileType::UNKNOWN);
    void GetRegionFileContents(std::string& full_name, std::vector<std::string>& file_contents);

    void GetRelativePath(std::string& folder);
    bool CheckPermissionForDirectory(std::string prefix);
    bool CheckPermissionForEntry(const std::string& entry);
    std::string GetCasacoreTypeString(casacore::ImageOpener::ImageTypes type); // convert enum to string
    CARTA::FileType GetRegionType(const std::string& filename);                // parse first line for CRTF or DS9

    // lock on file list handler
    tbb::mutex _file_list_mutex;
    tbb::mutex _region_list_mutex;
    std::string _filelist_folder;
    std::string _regionlist_folder;

    // permissions
    std::unordered_map<std::string, std::vector<std::string>>& _permissions_map;
    bool _permissions_enabled;
    std::string _api_key;

    bool _verbose_logging;
    std::string _root_folder, _base_folder;
};

#endif // CARTA_BACKEND__FILELISTHANDLER_H_
