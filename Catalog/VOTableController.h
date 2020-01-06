#ifndef CARTA_BACKEND__VOTABLECONTROLLER_H_
#define CARTA_BACKEND__VOTABLECONTROLLER_H_

#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "VOTableInterface.h"

namespace catalog {

class VOTableCarrier;

class Controller {
    const int _default_preview_row_numbers = 50;

public:
    Controller(){};
    ~Controller();

    static void OnFileListRequest(FileListRequest file_list_request, FileListResponse& file_list_response);
    static void OnFileInfoRequest(FileInfoRequest file_info_request, FileInfoResponse& file_info_response);
    void OnOpenFileRequest(OpenFileRequest open_file_request, OpenFileResponse& open_file_response);
    void OnCloseFileRequest(CloseFileRequest close_file_request);
    void OnFilterRequest(FilterRequest filter_request, std::function<void(FilterResponse)> partial_results_callback);

private:
    static std::string GetCurrentWorkingPath();
    static std::string GetFileSize(std::string file_path_name);
    static void ParseBasePath(std::string& file_path_name);
    static std::string Concatenate(std::string directory, std::string filename);
    void CloseFile(int file_id);

    std::unordered_map<int, VOTableCarrier*> _carriers; // The unordered map for <File Id, VOTableCarrier Ptr>
};

} // namespace catalog

#endif // CARTA_BACKEND__VOTABLECONTROLLER_H_