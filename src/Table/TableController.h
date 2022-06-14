/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018 - 2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_TABLE_TABLECONTROLLER_H_
#define CARTA_BACKEND_TABLE_TABLECONTROLLER_H_

#include <functional>
#include <string>
#include <unordered_map>

#include <carta-protobuf/catalog_file_info.pb.h>
#include <carta-protobuf/catalog_filter.pb.h>
#include <carta-protobuf/catalog_list.pb.h>
#include <carta-protobuf/open_catalog_file.pb.h>

#include "Table.h"
#include "Util/FileSystem.h"

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
    TableController(const std::string& top_level_folder, const std::string& starting_folder);
    void OnFileListRequest(const CARTA::CatalogListRequest& file_list_request, CARTA::CatalogListResponse& file_list_response);
    void OnFileInfoRequest(const CARTA::CatalogFileInfoRequest& file_info_request, CARTA::CatalogFileInfoResponse& file_info_response);
    void OnOpenFileRequest(const CARTA::OpenCatalogFile& open_file_request, CARTA::OpenCatalogFileAck& open_file_response);
    void OnCloseFileRequest(const CARTA::CloseCatalogFile& close_file_request);
    void OnFilterRequest(const CARTA::CatalogFilterRequest& filter_request,
        std::function<void(const CARTA::CatalogFilterResponse&)> partial_results_callback);

    void StopGettingFileList() {
        _stop_getting_file_list = true;
    }
    void SetProgressCallBack(const std::function<void(CARTA::ListProgress)>& progress_callback) {
        _progress_callback = progress_callback;
    }

protected:
    void PopulateHeaders(google::protobuf::RepeatedPtrField<CARTA::CatalogHeader>* headers, const Table& table);
    void ApplyFilter(const CARTA::FilterConfig& filter_config, TableView& view);
    static bool FilterParamsChanged(const std::vector<CARTA::FilterConfig>& filter_configs, std::string sort_column,
        CARTA::SortingType sorting_type, const TableViewCache& cached_config);
    fs::path GetPath(std::string directory, std::string name = "");
    std::string _top_level_folder;
    std::string _starting_folder;
    std::unordered_map<int, Table> _tables;
    std::unordered_map<int, TableViewCache> _view_cache;

private:
    volatile bool _stop_getting_file_list;
    volatile bool _first_report_made;
    std::function<void(CARTA::ListProgress)> _progress_callback;
};
} // namespace carta
#endif // CARTA_BACKEND_TABLE_TABLECONTROLLER_H_
