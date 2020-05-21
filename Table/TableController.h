#ifndef CARTA_BACKEND_TABLE_TABLECONTROLLER_H_
#define CARTA_BACKEND_TABLE_TABLECONTROLLER_H_

#include <string>
#include <unordered_map>

#include <carta-protobuf/open_catalog_file.pb.h>

#include "Table.h"

#define TABLE_PREVIEW_ROWS 50

namespace carta {

class TableController {
public:
    TableController(std::string root);
    void OnOpenFileRequest(const CARTA::OpenCatalogFile& open_file_request, CARTA::OpenCatalogFileAck& open_file_response);
    void OnCloseFileRequest(const CARTA::CloseCatalogFile& close_file_request);

protected:
    std::string _root_folder;
    std::unordered_map<int, Table> tables;
};
} // namespace carta
#endif // CARTA_BACKEND_TABLE_TABLECONTROLLER_H_
