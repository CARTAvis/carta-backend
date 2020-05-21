#include "TableController.h"

#include <fmt/format.h>

#include <filesystem>

using namespace carta;
using namespace std;

TableController::TableController(string root) : _root_folder(root) {}

CARTA::EntryType FromDataType(carta::DataType T) {
    switch (T) {
        case carta::INT8:
        case carta::INT16:
        case carta::INT32:
        case carta::UINT8:
        case carta::UINT16:
        case carta::UINT32:
            return CARTA::INT;
        case carta::INT64:
        case carta::UINT64:
            return CARTA::LONGLONG;
        case carta::FLOAT:
            return CARTA::FLOAT;
        case carta::DOUBLE:
            return CARTA::DOUBLE;
        case carta::BOOL:
            return CARTA::BOOL;
        case carta::STRING:
            return CARTA::STRING;
        default:
            return CARTA::UNKNOWN_TYPE;
    }
}

void TableController::OnOpenFileRequest(const CARTA::OpenCatalogFile& open_file_request, CARTA::OpenCatalogFileAck& open_file_response) {
    int file_id = open_file_request.file_id();
    int num_preview_rows(open_file_request.preview_data_size());
    if (num_preview_rows < 1) {
        num_preview_rows = TABLE_PREVIEW_ROWS;
    }

    open_file_response.set_file_id(file_id);

    filesystem::path file_path(_root_folder);

    if (!open_file_request.directory().empty()) {
        file_path /= open_file_request.directory();
    }
    file_path /= open_file_request.name();

    if (filesystem::exists(file_path) && filesystem::is_regular_file(file_path)) {
        // Close existing table with the same ID if it exists
        if (tables.count(file_id)) {
            tables.erase(file_id);
        }
        tables.emplace(file_id, file_path.string());
        Table& table = tables.at(file_id);
        open_file_response.set_success(table.IsValid());
        TableView view = table.View();

        auto file_info = open_file_response.mutable_file_info();
        file_info->set_name(open_file_request.name());
        // TODO: fill in the rest of the file info
        file_info->set_description("TODO");
        file_info->set_type(CARTA::CatalogFileType::VOTable);

        // Fill the number of raws
        size_t total_row_number = table.NumRows();
        if (num_preview_rows > total_row_number) {
            num_preview_rows = total_row_number;
        }
        open_file_response.set_data_size(total_row_number);

        int num_columns = table.NumColumns();

        int bool_counter = 0;
        int int_counter = 0;
        int longlong_counter = 0;
        int float_counter = 0;
        int double_counter = 0;
        int string_counter = 0;

        auto column_data = open_file_response.mutable_columns_data();
        for (auto i = 0; i < num_columns; i++) {
            auto col = table[i];
            if (col) {
                auto header = open_file_response.add_headers();
                auto data_type = FromDataType(col->data_type);
                header->set_data_type(data_type);

                // TODO: this can all get cleaned up after ICD changes are implemented
                if (data_type == CARTA::BOOL) {
                    header->set_data_type_index(bool_counter);
                    auto bool_column = column_data->add_bool_column();
                    auto vals = view.Values<bool>(col, 0, num_preview_rows);
                    *(bool_column->mutable_bool_column()) = {vals.begin(), vals.end()};
                    bool_counter++;
                } else if (data_type == CARTA::INT) {
                    header->set_data_type_index(int_counter);
                    auto int_column = column_data->add_int_column();
                    auto vals = view.Values<int32_t>(col, 0, num_preview_rows);
                    // TODO: handle short types etc
                    *(int_column->mutable_int_column()) = {vals.begin(), vals.end()};
                    int_counter++;
                } else if (data_type == CARTA::LONGLONG) {
                    header->set_data_type_index(longlong_counter);
                    auto ll_column = column_data->add_ll_column();
                    auto vals = view.Values<int64_t>(col, 0, num_preview_rows);
                    // TODO: handle unsigned types
                    *(ll_column->mutable_ll_column()) = {vals.begin(), vals.end()};
                    longlong_counter++;
                } else if (data_type == CARTA::FLOAT) {
                    header->set_data_type_index(float_counter);
                    auto float_column = column_data->add_float_column();
                    auto vals = view.Values<float>(col, 0, num_preview_rows);
                    *(float_column->mutable_float_column()) = {vals.begin(), vals.end()};
                    float_counter++;
                } else if (data_type == CARTA::DOUBLE) {
                    header->set_data_type_index(double_counter);
                    auto double_column = column_data->add_double_column();
                    auto vals = view.Values<double>(col, 0, num_preview_rows);
                    *(double_column->mutable_double_column()) = {vals.begin(), vals.end()};
                    double_counter++;
                } else if (data_type == CARTA::STRING) {
                    header->set_data_type_index(string_counter);
                    auto string_column = column_data->add_string_column();
                    auto vals = view.Values<string>(col, 0, num_preview_rows);
                    *(string_column->mutable_string_column()) = {vals.begin(), vals.end()};
                    string_counter++;
                }

                header->set_name(col->name);
                header->set_description(col->description);
                header->set_units(col->unit);
                header->set_column_index(i);
            }
        }
    } else {
        string path_string = file_path.string();
        open_file_response.set_message(fmt::format("Cannot find path {}", path_string));
        open_file_response.set_success(false);
        return;
    }
}
void TableController::OnCloseFileRequest(const CARTA::CloseCatalogFile& close_file_request) {
    auto file_id = close_file_request.file_id();
    if (tables.count(file_id)) {
        tables.erase(file_id);
    }
}
