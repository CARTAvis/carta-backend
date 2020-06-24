#ifndef CARTA_BACKEND_TABLE_TABLECONTROLLER_H_
#define CARTA_BACKEND_TABLE_TABLECONTROLLER_H_

#include <boost/filesystem.hpp>
#include <functional>
#include <string>
#include <unordered_map>

#include <carta-protobuf/catalog_file_info.pb.h>
#include <carta-protobuf/catalog_filter.pb.h>
#include <carta-protobuf/catalog_list.pb.h>
#include <carta-protobuf/open_catalog_file.pb.h>

#include "Table.h"

#define TABLE_PREVIEW_ROWS 50

namespace carta {

struct TableViewCache {
    TableView view;
    std::vector<CARTA::FilterConfig> filter_configs;
    std::string sort_column;
    CARTA::SortingType sorting_type;
};

class TableController {
public:
    TableController(const std::string& root, const std::string& base);
    void OnFileListRequest(const CARTA::CatalogListRequest& file_list_request, CARTA::CatalogListResponse& file_list_response);
    void OnFileInfoRequest(const CARTA::CatalogFileInfoRequest& file_info_request, CARTA::CatalogFileInfoResponse& file_info_response);
    void OnOpenFileRequest(const CARTA::OpenCatalogFile& open_file_request, CARTA::OpenCatalogFileAck& open_file_response);
    void OnCloseFileRequest(const CARTA::CloseCatalogFile& close_file_request);
    void OnFilterRequest(const CARTA::CatalogFilterRequest& filter_request,
        std::function<void(const CARTA::CatalogFilterResponse&)> partial_results_callback);

protected:
    void PopulateHeaders(google::protobuf::RepeatedPtrField<CARTA::CatalogHeader>* headers, const Table& table);
    void ApplyFilter(const CARTA::FilterConfig& filter_config, TableView& view);
    static bool FilterParamsChanged(const std::vector<CARTA::FilterConfig>& filter_configs, std::string sort_column,
        CARTA::SortingType sorting_type, const TableViewCache& cached_config);
    boost::filesystem::path GetPath(std::string directory, std::string name = "");
    std::string _root_folder;
    std::string _base_folder;
    std::unordered_map<int, Table> _tables;
    std::unordered_map<int, TableViewCache> _view_cache;
};
} // namespace carta
#endif // CARTA_BACKEND_TABLE_TABLECONTROLLER_H_
