#include "VOTableController.h"

#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>

#include "VOTableCarrier.h"
#include "VOTableParser.h"

using namespace catalog;

Controller::Controller(std::string root) : _root_folder(root) {}

Controller::~Controller() {
    for (auto& carrier : _carriers) {
        carrier.second->DisconnectCalled();
        delete carrier.second;
        carrier.second = nullptr;
    }
    _carriers.clear();
}

void Controller::OnFileListRequest(CARTA::CatalogListRequest file_list_request, CARTA::CatalogListResponse& file_list_response) {
    bool success(false);
    std::string message;
    std::string directory(file_list_request.directory()); // Note that it is the relative path!

    // Parse the relative path to the absolute path
    ParseBasePath(directory);

    // Get a list of files under the directory
    DIR* current_path;
    struct dirent* current_entry;
    if ((current_path = opendir(directory.c_str()))) {
        success = true; // The directory exists
        while ((current_entry = readdir(current_path))) {
            if (strcmp(current_entry->d_name, ".") != 0 && strcmp(current_entry->d_name, "..") != 0) {
                std::string file_name = current_entry->d_name;
                // Check is it a XML file
                if (IsVOTableFile(file_name)) {
                    // Check is it a VOTable XML file
                    std::string path_name = Concatenate(directory, file_name);
                    if (VOTableParser::IsVOTable(path_name)) {
                        // Fill the file info
                        auto file_info = file_list_response.add_files();
                        file_info->set_name(file_name);
                        file_info->set_type(CARTA::CatalogFileType::VOTable);
                        file_info->set_file_size(GetFileByteSize(path_name));
                    }
                } else {
                    // Check is it a sub-directory
                    DIR* sub_path;
                    std::string sub_directory = Concatenate(directory, current_entry->d_name);
                    if ((sub_path = opendir(sub_directory.c_str()))) {
                        std::size_t found = sub_directory.find_last_of("/");
                        file_list_response.add_subdirectories(sub_directory.substr(found + 1));
                        closedir(sub_path);
                    }
                }
            }
        }
        closedir(current_path);
    } else {
        message = "Can not open the directory: " + directory;
        return;
    }

    // Fill the file list response
    file_list_response.set_success(success);
    file_list_response.set_message(message);
    GetRelativePath(directory);
    file_list_response.set_directory(directory);

    // Get the directory parent
    std::string parent_directory;
    if (directory.find("/") != std::string::npos) {
        parent_directory = directory.substr(0, directory.find_last_of("/"));
    }
    GetRelativePath(parent_directory);
    file_list_response.set_parent(parent_directory);
}

void Controller::OnFileInfoRequest(CARTA::CatalogFileInfoRequest file_info_request, CARTA::CatalogFileInfoResponse& file_info_response) {
    bool success(false);
    std::string message;
    std::string directory(file_info_request.directory());
    std::string filename(file_info_request.name());
    std::string file_path_name = Concatenate(directory, filename);
    ParseBasePath(file_path_name);

    // Get the VOTable data (only read to the headers)
    VOTableCarrier carrier = VOTableCarrier();
    VOTableParser parser(file_path_name, &carrier, true);
    if (carrier.IsValid()) {
        success = true;
    } else {
        message = "Can not load the file: " + file_path_name;
        return;
    }

    // Fill the file info response
    file_info_response.set_success(success);
    file_info_response.set_message(message);
    auto file_info = file_info_response.mutable_file_info();
    file_info->set_name(filename);
    file_info->set_type(CARTA::CatalogFileType::VOTable);
    file_info->set_file_size(GetFileByteSize(file_path_name));
    file_info->set_description(carrier.GetFileDescription());
    carrier.GetHeaders(file_info_response);
    carrier.GetCooosys(file_info);
}

void Controller::OnOpenFileRequest(CARTA::OpenCatalogFile open_file_request, CARTA::OpenCatalogFileAck& open_file_response) {
    bool success(false);
    std::string message;
    std::string directory(open_file_request.directory());
    std::string filename(open_file_request.name());
    std::string file_path_name = Concatenate(directory, filename);
    ParseBasePath(file_path_name);

    int file_id(open_file_request.file_id());
    int preview_data_size(open_file_request.preview_data_size());
    if (preview_data_size < 1) {
        preview_data_size = _default_preview_row_numbers; // Default preview row numbers
    }

    // Get the VOTable data (read the whole file)
    VOTableCarrier* carrier = new VOTableCarrier();
    VOTableParser parser(file_path_name, carrier);
    if (carrier->IsValid()) {
        success = true;
    } else {
        message = "Can not load the file: " + file_path_name;
        return;
    }

    // Fill the response of opening a file
    open_file_response.set_success(success);
    open_file_response.set_message(message);
    open_file_response.set_file_id(file_id);
    auto file_info = open_file_response.mutable_file_info();
    file_info->set_name(filename);
    file_info->set_type(CARTA::CatalogFileType::VOTable);
    file_info->set_description(GetFileSize(file_path_name));
    carrier->GetCooosys(file_info);

    // Fill the number of raws
    size_t total_row_number = carrier->GetTableRowNumber();
    if (preview_data_size > total_row_number) {
        preview_data_size = total_row_number;
    }
    open_file_response.set_data_size(total_row_number);

    // Fill the table headers and their columns data
    carrier->GetHeadersAndData(open_file_response, preview_data_size);

    // Move the VOTableCarrier with respect to its file_id to the cache
    CloseFile(file_id);
    std::unique_lock<std::mutex> lock(_carriers_mutex);
    _carriers[file_id] = std::move(carrier);
}

void Controller::OnCloseFileRequest(CARTA::CloseCatalogFile close_file_request) {
    int file_id(close_file_request.file_id());
    CloseFile(file_id);
}

void Controller::OnFilterRequest(
    CARTA::CatalogFilterRequest filter_request, std::function<void(CARTA::CatalogFilterResponse)> partial_results_callback) {
    int file_id(filter_request.file_id());
    if (!_carriers.count(file_id)) {
        std::cerr << "VOTable file does not exist (file ID: " << file_id << "!" << std::endl;
        return;
    }
    _carriers[file_id]->IncreaseStreamCount();
    _carriers[file_id]->GetFilteredData(
        filter_request, [&](CARTA::CatalogFilterResponse filter_response) { partial_results_callback(filter_response); });
    _carriers[file_id]->DecreaseStreamCount();
}

bool Controller::IsVOTableFile(std::string file_name) {
    bool result(false);
    std::size_t found = file_name.find_last_of(".");
    if (found < file_name.length()) {
        if ((file_name.substr(found + 1) == "xml") || (file_name.substr(found + 1) == "vot") ||
            (file_name.substr(found + 1) == "votable")) {
            result = true;
        }
    }
    return result;
}

std::string Controller::GetFileSize(std::string file_path_name) {
    struct stat file_status;
    stat(file_path_name.c_str(), &file_status);
    return (std::to_string(file_status.st_size) + " (bytes)");
}

int64_t Controller::GetFileByteSize(std::string file_path_name) {
    struct stat file_status;
    stat(file_path_name.c_str(), &file_status);
    return file_status.st_size;
}

void Controller::ParseBasePath(std::string& file_path_name) {
    std::string root_folder = _root_folder;
    root_folder.replace(0, 1, ""); // Remove "/" at the start of root path name
    // Replace the "$BASE" with "."
    std::string alias_base_path("$BASE");
    if (file_path_name.find(alias_base_path) == 0) { // For the path name "$BASE/images"
        file_path_name.replace(file_path_name.find(alias_base_path), alias_base_path.length(), ".");
    } else if (!root_folder.empty() && file_path_name.find(root_folder) != 0) {
        // For the path name "base/path/images"
        file_path_name = _root_folder + "/" + file_path_name; // Change to "/root/path/base/path/images"
    } else {                                                  // For the path "root/path/base/path/images"
        file_path_name = "/" + file_path_name;                // Change to "/root/path/base/path/images"
    }
}

std::string Controller::Concatenate(std::string directory, std::string filename) {
    std::string file_path_name;
    if (directory.empty()) {
        file_path_name = filename;
    } else {
        file_path_name = directory + "/" + filename;
    }
    return file_path_name;
}

void Controller::CloseFile(int file_id) {
    if (_carriers.count(file_id)) {
        _carriers[file_id]->DisconnectCalled();
        delete _carriers[file_id];
        _carriers[file_id] = nullptr;
        _carriers.erase(file_id);
    }
}

void Controller::GetRelativePath(std::string& folder) {
    // Remove root folder path from given folder string
    if (folder.find("./") == 0) {
        folder.replace(0, 2, ""); // remove leading "./"
    } else if (folder.find(_root_folder) == 0) {
        folder.replace(0, _root_folder.length(), ""); // remove root folder path
        if (folder.front() == '/') {
            folder.replace(0, 1, ""); // remove leading '/'
        }
    }
    if (folder.empty()) {
        folder = ".";
    }
}

// Print functions for the protobuf message

void Controller::Print(CARTA::CatalogListRequest file_list_request) {
    std::cout << "CatalogListRequest:" << std::endl;
    std::cout << "directory: " << file_list_request.directory() << std::endl;
    std::cout << std::endl;
}

void Controller::Print(CARTA::CatalogListResponse file_list_response) {
    std::cout << "CatalogListResponse:" << std::endl;
    std::cout << "success:   " << GetBoolType(file_list_response.success()) << std::endl;
    std::cout << "message:   " << file_list_response.message() << std::endl;
    std::cout << "directory: " << file_list_response.directory() << std::endl;
    std::cout << "parent:    " << file_list_response.parent() << std::endl;
    for (int i = 0; i < file_list_response.files_size(); ++i) {
        std::cout << "files(" << i << "):" << std::endl;
        auto file = file_list_response.files(i);
        Print(file);
    }
    for (int i = 0; i < file_list_response.subdirectories_size(); ++i) {
        std::cout << "subdirectories(" << i << "): " << file_list_response.subdirectories(i) << std::endl;
    }
    std::cout << std::endl;
}

void Controller::Print(CARTA::CatalogFileInfo file_info) {
    std::cout << "name:        " << file_info.name() << std::endl;
    std::cout << "type:        " << GetFileType(file_info.type()) << std::endl;
    std::cout << "file_size:   " << file_info.file_size() << " (Byte)" << std::endl;
    std::cout << "description: " << file_info.description() << std::endl;
    for (int i = 0; i < file_info.coosys().size(); ++i) {
        auto coosys = file_info.coosys(i);
        std::cout << "Coosys(" << i << "):" << std::endl;
        std::cout << "    equinox: " << coosys.equinox() << std::endl;
        std::cout << "    epoch:   " << coosys.epoch() << std::endl;
        std::cout << "    system:  " << coosys.system() << std::endl;
        std::cout << std::endl;
    }
}

void Controller::Print(CARTA::CatalogFileInfoRequest file_info_request) {
    std::cout << "CARTA::CatalogFileInfoRequest:" << std::endl;
    std::cout << "directory: " << file_info_request.directory() << std::endl;
    std::cout << "name:      " << file_info_request.name() << std::endl;
    std::cout << std::endl;
}

void Controller::Print(CARTA::CatalogFileInfoResponse file_info_response) {
    std::cout << "CARTA::CatalogFileInfoResponse:" << std::endl;
    std::cout << "success:   " << GetBoolType(file_info_response.success()) << std::endl;
    std::cout << "message:   " << file_info_response.message() << std::endl;
    std::cout << "file_info: " << std::endl;
    Print(file_info_response.file_info());
    for (int i = 0; i < file_info_response.headers_size(); ++i) {
        std::cout << "headers(" << i << "):" << std::endl;
        auto header = file_info_response.headers(i);
        Print(header);
    }
    std::cout << std::endl;
}

void Controller::Print(CARTA::CatalogHeader header) {
    std::cout << "CARTA::CatalogHeader:" << std::endl;
    std::cout << "name:            " << header.name() << std::endl;
    std::cout << "data_type:       " << GetDataType(header.data_type()) << std::endl;
    std::cout << "column_index:    " << header.column_index() << std::endl;
    std::cout << "data_type_index: " << header.data_type_index() << std::endl;
    std::cout << "description:     " << header.description() << std::endl;
    std::cout << "units:           " << header.units() << std::endl;
    std::cout << std::endl;
}

void Controller::Print(CARTA::OpenCatalogFile open_file_request) {
    std::cout << "CARTA::OpenCatalogFile:" << std::endl;
    std::cout << "directory:         " << open_file_request.directory() << std::endl;
    std::cout << "name:              " << open_file_request.name() << std::endl;
    std::cout << "file_id:           " << open_file_request.file_id() << std::endl;
    std::cout << "preview_data_size: " << open_file_request.preview_data_size() << std::endl;
    std::cout << std::endl;
}

void Controller::Print(CARTA::OpenCatalogFileAck open_file_response) {
    std::cout << "CARTA::OpenCatalogFileAck" << std::endl;
    std::cout << "success:   " << GetBoolType(open_file_response.success()) << std::endl;
    std::cout << "message:   " << open_file_response.message() << std::endl;
    std::cout << "file_id:   " << open_file_response.file_id() << std::endl;
    Print(open_file_response.file_info());
    std::cout << "data_size: " << open_file_response.data_size() << std::endl;
    for (int i = 0; i < open_file_response.headers_size(); ++i) {
        std::cout << "headers(" << i << "):" << std::endl;
        Print(open_file_response.headers(i));
    }
    Print(open_file_response.columns_data());
    std::cout << std::endl;
}

void Controller::Print(CARTA::CatalogColumnsData columns_data) {
    for (int i = 0; i < columns_data.bool_column_size(); ++i) {
        std::cout << "bool_columns(" << i << "):" << std::endl;
        auto column = columns_data.bool_column(i);
        for (int j = 0; j < column.bool_column_size(); ++j) {
            std::cout << column.bool_column(j) << " | ";
        }
        std::cout << std::endl;
    }
    for (int i = 0; i < columns_data.string_column_size(); ++i) {
        std::cout << "string_columns(" << i << "):" << std::endl;
        auto column = columns_data.string_column(i);
        for (int j = 0; j < column.string_column_size(); ++j) {
            std::cout << column.string_column(j) << " | ";
        }
        std::cout << std::endl;
    }
    for (int i = 0; i < columns_data.int_column_size(); ++i) {
        std::cout << "int_columns(" << i << "):" << std::endl;
        auto column = columns_data.int_column(i);
        for (int j = 0; j < column.int_column_size(); ++j) {
            std::cout << column.int_column(j) << " | ";
        }
        std::cout << std::endl;
    }
    for (int i = 0; i < columns_data.ll_column_size(); ++i) {
        std::cout << "ll_columns(" << i << "):" << std::endl;
        auto column = columns_data.ll_column(i);
        for (int j = 0; j < column.ll_column_size(); ++j) {
            std::cout << column.ll_column(j) << " | ";
        }
        std::cout << std::endl;
    }
    for (int i = 0; i < columns_data.float_column_size(); ++i) {
        std::cout << "float_columns(" << i << "):" << std::endl;
        auto column = columns_data.float_column(i);
        for (int j = 0; j < column.float_column_size(); ++j) {
            std::cout << std::setprecision(10) << column.float_column(j) << " | ";
        }
        std::cout << std::endl;
    }
    for (int i = 0; i < columns_data.double_column_size(); ++i) {
        std::cout << "double_columns(" << i << "):" << std::endl;
        auto column = columns_data.double_column(i);
        for (int j = 0; j < column.double_column_size(); ++j) {
            std::cout << std::setprecision(10) << column.double_column(j) << " | ";
        }
        std::cout << std::endl;
    }
}

void Controller::Print(CARTA::CloseCatalogFile close_file_request) {
    std::cout << "CARTA::CloseCatalogFile:" << std::endl;
    std::cout << "file_id: " << close_file_request.file_id() << std::endl;
    std::cout << std::endl;
}

void Controller::Print(CARTA::CatalogFilterRequest filter_request) {
    std::cout << "CARTA::CatalogFilterRequest:" << std::endl;
    std::cout << "file_id:           " << filter_request.file_id() << std::endl;
    std::cout << "hided_headers:     " << std::endl;
    for (int i = 0; i < filter_request.hided_headers_size(); ++i) {
        std::cout << filter_request.hided_headers(i) << " | ";
    }
    std::cout << std::endl;
    for (int i = 0; i < filter_request.filter_configs_size(); ++i) {
        std::cout << "filter_config(" << i << "):" << std::endl;
        auto filter = filter_request.filter_configs(i);
        Print(filter);
    }
    std::cout << "subset_data_size:   " << filter_request.subset_data_size() << std::endl;
    std::cout << "subset_start_index: " << filter_request.subset_start_index() << std::endl;
    Print(filter_request.image_bounds());
    std::cout << "image_file_id:      " << filter_request.image_file_id() << std::endl;
    std::cout << "region_id:          " << filter_request.region_id() << std::endl;
    std::cout << "sort_column:        " << filter_request.sort_column() << std::endl;
    std::cout << "sorting_type:       " << GetSortingType(filter_request.sorting_type()) << std::endl;
    std::cout << std::endl;
}

void Controller::Print(CARTA::FilterConfig filter_config) {
    std::cout << "CARTA::FilterConfig:" << std::endl;
    std::cout << "column_name:         " << filter_config.column_name() << std::endl;
    std::cout << "comparison_operator: " << GetComparisonOperator(filter_config.comparison_operator()) << std::endl;
    std::cout << "min:                 " << filter_config.min() << std::endl;
    std::cout << "max:                 " << filter_config.max() << std::endl;
    std::cout << "sub_string:          " << filter_config.sub_string() << std::endl;
    std::cout << std::endl;
}

void Controller::Print(CARTA::CatalogImageBounds catalog_image_bounds) {
    std::cout << "CARTA::CatalogImageBounds:" << std::endl;
    std::cout << "x_column_name: " << catalog_image_bounds.x_column_name() << std::endl;
    std::cout << "y_column_name: " << catalog_image_bounds.y_column_name() << std::endl;
    auto image_bounds = catalog_image_bounds.image_bounds();
    std::cout << "x_min: " << image_bounds.x_min() << std::endl;
    std::cout << "x_max: " << image_bounds.x_max() << std::endl;
    std::cout << "y_min: " << image_bounds.y_min() << std::endl;
    std::cout << "y_max: " << image_bounds.y_max() << std::endl;
    std::cout << std::endl;
}

void Controller::Print(CARTA::CatalogFilterResponse filter_response) {
    std::cout << "CARTA::CatalogFilterResponse:" << std::endl;
    std::cout << "file_id:       " << filter_response.file_id() << std::endl;
    std::cout << "image_file_id: " << filter_response.image_file_id() << std::endl;
    std::cout << "region_id:     " << filter_response.region_id() << std::endl;
    Print(filter_response.columns_data());
    std::cout << "subset_data_size: " << filter_response.subset_data_size() << std::endl;
    std::cout << "subset_end_index: " << filter_response.subset_end_index() << std::endl;
    std::cout << "progress:  " << filter_response.progress() << std::endl;
    std::cout << std::endl;
}

std::string Controller::GetDataType(CARTA::EntryType data_type) {
    std::string result;
    switch (data_type) {
        case CARTA::EntryType::BOOL:
            result = "bool";
            break;
        case CARTA::EntryType::STRING:
            result = "string";
            break;
        case CARTA::EntryType::INT:
            result = "int";
            break;
        case CARTA::EntryType::LONGLONG:
            result = "long long";
            break;
        case CARTA::EntryType::FLOAT:
            result = "float";
            break;
        case CARTA::EntryType::DOUBLE:
            result = "double";
            break;
        default:
            result = "unknown data type";
            break;
    }
    return result;
}

std::string Controller::GetBoolType(bool bool_type) {
    std::string result;
    if (bool_type) {
        result = "true";
    } else {
        result = "false";
    }
    return result;
}

std::string Controller::GetFileType(CARTA::CatalogFileType file_type) {
    std::string result;
    switch (file_type) {
        case CARTA::CatalogFileType::VOTable:
            result = "VOTable";
            break;
        default:
            result = "unknown Catalog file type";
            break;
    }
    return result;
}

std::string Controller::GetComparisonOperator(CARTA::ComparisonOperator comparison_operator) {
    std::string result;
    switch (comparison_operator) {
        case CARTA::ComparisonOperator::EqualTo:
            result = "==";
            break;
        case CARTA::ComparisonOperator::NotEqualTo:
            result = "!=";
            break;
        case CARTA::ComparisonOperator::LessThan:
            result = "<";
            break;
        case CARTA::ComparisonOperator::GreaterThan:
            result = ">";
            break;
        case CARTA::ComparisonOperator::LessThanOrEqualTo:
            result = "<=";
            break;
        case CARTA::ComparisonOperator::GreaterThanOrEqualTo:
            result = ">=";
            break;
        case CARTA::ComparisonOperator::BetweenAnd:
            result = "...";
            break;
        case CARTA::ComparisonOperator::FromTo:
            result = "..";
            break;
        default:
            result = "unknown comparison operator!";
            break;
    }
    return result;
}

std::string Controller::GetSortingType(CARTA::SortingType sorting_type) {
    std::string result;
    switch (sorting_type) {
        case CARTA::SortingType::Ascend:
            result = "Ascend";
            break;
        case CARTA::SortingType::Descend:
            result = "Descend";
            break;
        default:
            result = "unknown sorting type";
            break;
    }
    return result;
}