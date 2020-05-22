#ifndef CARTA_BACKEND__VOTABLECONTROLLER_H_
#define CARTA_BACKEND__VOTABLECONTROLLER_H_

#include <carta-protobuf/catalog_file_info.pb.h>
#include <carta-protobuf/catalog_filter.pb.h>
#include <carta-protobuf/catalog_list.pb.h>
#include <carta-protobuf/open_catalog_file.pb.h>

#include <cmath>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace catalog {

class VOTableCarrier;

class Controller {
    std::string _root_folder;
    const int _default_preview_row_numbers = 50;

public:
    Controller(std::string root);
    ~Controller();

    void OnFileListRequest(CARTA::CatalogListRequest file_list_request, CARTA::CatalogListResponse& file_list_response);
    void OnFileInfoRequest(CARTA::CatalogFileInfoRequest file_info_request, CARTA::CatalogFileInfoResponse& file_info_response);
    void OnFilterRequest(
        CARTA::CatalogFilterRequest filter_request, std::function<void(CARTA::CatalogFilterResponse)> partial_results_callback);

private:
    bool IsVOTableFile(std::string file_name);
    std::string GetFileSize(std::string file_path_name);
    int64_t GetFileByteSize(std::string file_path_name);
    bool GetAbsBasePath(std::string& directory);
    std::string Concatenate(std::string directory, std::string filename);
    void CloseFile(int file_id);
    void GetRelativePath(std::string& folder);

    std::unordered_map<int, VOTableCarrier*> _carriers; // The unordered map for <File Id, VOTableCarrier Ptr>
    std::mutex _carriers_mutex;
};

} // namespace catalog

#endif // CARTA_BACKEND__VOTABLECONTROLLER_H_