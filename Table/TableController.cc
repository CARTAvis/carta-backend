#include "TableController.h"

#include <filesystem>

#include <fmt/format.h>

#include "../Util.h"

using namespace carta;
using namespace std;

TableController::TableController(string root) : _root_folder(root) {}

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

    if (!filesystem::exists(file_path) || !filesystem::is_regular_file(file_path)) {
        open_file_response.set_message(fmt::format("Cannot find path {}", file_path.string()));
        open_file_response.set_success(false);
        return;
    }

    // Close existing table with the same ID if it exists
    if (tables.count(file_id)) {
        tables.erase(file_id);
    }
    tables.emplace(file_id, file_path);
    Table& table = tables.at(file_id);

    if (!table.IsValid()) {
        open_file_response.set_success(false);
        open_file_response.set_message(table.ParseError());
        return;
    }

    TableView view = table.View();

    auto file_info = open_file_response.mutable_file_info();
    file_info->set_name(open_file_request.name());
    file_info->set_type(table.Type());
    file_info->set_file_size(filesystem::file_size(file_path));
    file_info->set_description(table.Description());

    // Fill the number of rows
    size_t total_row_number = table.NumRows();
    if (num_preview_rows > total_row_number) {
        num_preview_rows = total_row_number;
    }
    open_file_response.set_data_size(total_row_number);

    int num_columns = table.NumColumns();

    auto preview_data = open_file_response.mutable_preview_data();
    auto headers = open_file_response.mutable_headers();
    PopulateHeaders(headers, table);

    // Fill values of those columns that have a supported type
    for (auto i = 0; i < num_columns; i++) {
        auto col = table[i];
        if (col->data_type != CARTA::UnsupportedType) {
            (*preview_data)[i] = CARTA::ColumnData();
            view.FillValues(col, (*preview_data)[i], 0, num_preview_rows);
        }
    }

    open_file_response.set_success(true);
}

void TableController::OnCloseFileRequest(const CARTA::CloseCatalogFile& close_file_request) {
    auto file_id = close_file_request.file_id();
    if (tables.count(file_id)) {
        tables.erase(file_id);
    }
}

void TableController::OnFilterRequest(
    const CARTA::CatalogFilterRequest& filter_request, std::function<void(const CARTA::CatalogFilterResponse&)> partial_results_callback) {
    int file_id = filter_request.file_id();

    if (tables.count(file_id)) {
        Table& table = tables.at(file_id);
        auto view = table.View();

        // TODO: cache view results and compare before re-filtering and sorting
        for (auto& config : filter_request.filter_configs()) {
            ApplyFilter(config, view);
        }

        string sort_column_name = filter_request.sort_column();
        if (!sort_column_name.empty()) {
            auto sort_column = table[sort_column_name];
            view.SortByColumn(sort_column, filter_request.sorting_type() == CARTA::Ascending);
        }

        int start_index = filter_request.subset_start_index();
        int num_rows = filter_request.subset_data_size();

        CARTA::CatalogFilterResponse filter_response;
        filter_response.set_file_id(file_id);
        int num_results = view.NumRows();

        filter_response.set_filter_data_size(num_results);
        int response_size = min(num_rows, num_results - start_index);
        filter_response.set_request_end_index(start_index + response_size);

        auto num_columns = filter_request.column_indices_size();
        auto column_data = filter_response.mutable_columns();

        int max_chunk_size = 100000;
        int num_remaining_rows = response_size;
        int sent_rows = 0;
        int chunk_start_index = start_index;

        // Handle empty filters
        if (num_remaining_rows == 0) {
            filter_response.set_subset_data_size(0);
            filter_response.set_progress(1.0f);
            filter_response.set_subset_end_index(start_index);
            partial_results_callback(filter_response);
            return;
        }

        while (num_remaining_rows > 0) {
            int chunk_size = min(num_remaining_rows, max_chunk_size);
            int chunk_end_index = chunk_start_index + chunk_size;
            filter_response.set_subset_data_size(chunk_size);
            filter_response.set_subset_end_index(chunk_end_index);

            for (auto i = 0; i < num_columns; i++) {
                auto index = filter_request.column_indices()[i];
                auto col = table[index];
                if (col && col->data_type != CARTA::UnsupportedType) {
                    (*column_data)[index] = CARTA::ColumnData();
                    view.FillValues(col, (*column_data)[index], chunk_start_index, chunk_end_index);
                }
            }

            sent_rows += chunk_size;
            chunk_start_index += chunk_size;
            num_remaining_rows -= chunk_size;
            if (num_remaining_rows <= 0) {
                filter_response.set_progress(1.0f);
            } else {
                filter_response.set_progress(sent_rows / float(response_size));
            }

            partial_results_callback(filter_response);
        }
    }
}

void TableController::OnFileListRequest(
    const CARTA::CatalogListRequest& file_list_request, CARTA::CatalogListResponse& file_list_response) {
    string directory = file_list_request.directory();

    // Strip meaningless directory paths
    if (directory == "." || directory == "./") {
        directory = "";
    }

    // Remove leading /
    auto start_index = directory.find_first_not_of('/');
    if (start_index != 0 && start_index != string::npos) {
        directory = directory.substr(start_index);
    }

    filesystem::path root_path(_root_folder);
    filesystem::path file_path(_root_folder);

    if (!directory.empty()) {
        file_path /= directory;
    }

    if (!filesystem::exists(file_path) || !filesystem::is_directory(file_path)) {
        file_list_response.set_success(false);
        file_list_response.set_message("Incorrect file path");
        return;
    }

    auto relative_path = filesystem::relative(file_path, root_path);
    file_list_response.set_directory(relative_path);

    auto parent_path = filesystem::relative(file_path.parent_path(), root_path);
    file_list_response.set_parent(parent_path);

    for (const auto& entry : filesystem::directory_iterator(file_path)) {
        if (entry.is_directory()) {
            file_list_response.add_subdirectories(entry.path().filename());
        } else if (entry.is_regular_file() && entry.exists()) {
            uint32_t file_magic_number = GetMagicNumber(entry.path());
            CARTA::CatalogFileType file_type;
            if (file_magic_number == XML_MAGIC_NUMBER) {
                file_type = CARTA::VOTable;
            } else if (file_magic_number == FITS_MAGIC_NUMBER) {
                file_type = CARTA::FITSTable;
            } else {
                continue;
            }

            // Fill the file info
            auto file_info = file_list_response.add_files();
            file_info->set_name(entry.path().filename());
            file_info->set_type(file_type);
            file_info->set_file_size(entry.file_size());
        }
    }

    file_list_response.set_success(true);
}

void TableController::OnFileInfoRequest(
    const CARTA::CatalogFileInfoRequest& file_info_request, CARTA::CatalogFileInfoResponse& file_info_response) {
    filesystem::path file_path(_root_folder);

    if (!file_info_request.directory().empty()) {
        file_path /= file_info_request.directory();
    }
    file_path /= file_info_request.name();

    if (!filesystem::exists(file_path) || !filesystem::is_regular_file(file_path)) {
        file_info_response.set_success(false);
        file_info_response.set_message("Incorrect file path");
        return;
    }

    Table table(file_path, true);

    if (!table.IsValid()) {
        file_info_response.set_success(false);
        file_info_response.set_message(table.ParseError());
        return;
    }

    auto file_info = file_info_response.mutable_file_info();
    file_info->set_name(file_path.filename());
    file_info->set_type(table.Type());
    file_info->set_file_size(filesystem::file_size(file_path));
    file_info->set_description(table.Description());

    int num_columns = table.NumColumns();
    for (auto i = 0; i < num_columns; i++) {
        auto col = table[i];
        if (col) {
            auto header = file_info_response.add_headers();
            header->set_data_type(col->data_type);
            header->set_name(col->name);
            header->set_description(col->description);
            header->set_units(col->unit);
            header->set_column_index(i);
        }
    }

    file_info_response.set_success(true);
}

void TableController::ApplyFilter(const CARTA::FilterConfig& filter_config, TableView& view) {
    string column_name = filter_config.column_name();
    auto column = view.GetTable()[column_name];
    if (!column) {
        fmt::print("Could not filter on non-existing column \"{}\"", column_name);
        return;
    }

    // Perform subset string filter
    if (column->data_type == CARTA::String) {
        view.StringFilter(column, filter_config.sub_string());
    } else {
        view.NumericFilter(column, filter_config.comparison_operator(), filter_config.value(), filter_config.secondary_value());
    }
}

void TableController::PopulateHeaders(google::protobuf::RepeatedPtrField<CARTA::CatalogHeader>* headers, const Table& table) {
    if (!headers) {
        return;
    }

    int num_columns = table.NumColumns();
    for (auto i = 0; i < num_columns; i++) {
        auto col = table[i];
        if (col) {
            auto header = headers->Add();
            header->set_data_type(col->data_type);
            header->set_name(col->name);
            header->set_description(col->description);
            header->set_units(col->unit);
            header->set_column_index(i);
        }
    }
}
