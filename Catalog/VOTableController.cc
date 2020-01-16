#include "VOTableController.h"

#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "VOTableCarrier.h"
#include "VOTableParser.h"

using namespace catalog;

Controller::~Controller() {
    std::unique_lock<std::mutex> lock(_carriers_mutex);
    for (auto& carrier : _carriers) {
        delete carrier.second;
        carrier.second = nullptr;
    }
    _carriers.clear();
}

void Controller::OnFileListRequest(CARTA::CatalogListRequest file_list_request, CARTA::CatalogListResponse& file_list_response) {
    bool success(false);
    std::string message;
    std::string directory(file_list_request.directory());

    // Replace the $BASE with current working path
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
                if (file_name.substr(file_name.find_last_of(".") + 1) == "xml") {
                    // Check is it a VOTable XML file
                    std::string path_name = Concatenate(directory, file_name);
                    if (VOTableParser::IsVOTable(path_name)) {
                        // Fill the file info
                        auto file_info = file_list_response.add_files();
                        file_info->set_name(file_name);
                        file_info->set_type(CARTA::CatalogFileType::VOTable);
                        file_info->set_file_size(GetFileKBSize(path_name));
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
    }

    // Get the directory parent
    std::string parent_directory;
    if (directory.find("/") != std::string::npos) {
        parent_directory = directory.substr(0, directory.find_last_of("/"));
    }

    // Fill the file list response
    file_list_response.set_success(success);
    file_list_response.set_message(message);
    file_list_response.set_directory(directory);
    file_list_response.set_parent(parent_directory);
}

void Controller::OnFileInfoRequest(CARTA::CatalogFileInfoRequest file_info_request, CARTA::CatalogFileInfoResponse& file_info_response) {
    bool success(false);
    std::string message;
    std::string directory(file_info_request.directory());
    std::string filename(file_info_request.name());
    std::string file_path_name = Concatenate(directory, filename);

    // Get the VOTable data (only read to the headers)
    VOTableCarrier carrier = VOTableCarrier();
    VOTableParser parser(file_path_name, &carrier, true);
    if (carrier.IsValid()) {
        success = true;
    } else {
        message = "Can not load the file: " + file_path_name;
    }

    // Fill the file info response
    file_info_response.set_success(success);
    file_info_response.set_message(message);
    auto file_info = file_info_response.mutable_file_info();
    file_info->set_name(filename);
    file_info->set_type(CARTA::CatalogFileType::VOTable);
    file_info->set_file_size(GetFileKBSize(file_path_name));
    file_info->set_description(carrier.GetFileDescription());
    carrier.GetHeaders(file_info_response);
    file_info_response.set_data_size(
        carrier.GetTableRowNumber()); // TODO: since we only read the headers, we don't know the number of table rows
}

void Controller::OnOpenFileRequest(CARTA::OpenCatalogFile open_file_request, CARTA::OpenCatalogFileAck& open_file_response) {
    bool success(false);
    std::string message;
    std::string directory(open_file_request.directory());
    std::string filename(open_file_request.name());
    std::string file_path_name = Concatenate(directory, filename);

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
    }

    // Fill the response of opening a file
    open_file_response.set_success(success);
    open_file_response.set_message(message);
    open_file_response.set_file_id(file_id);
    auto file_info = open_file_response.mutable_file_info();
    file_info->set_name(filename);
    file_info->set_type(CARTA::CatalogFileType::VOTable);
    file_info->set_description(GetFileSize(file_path_name));

    // Fill the number of raws
    size_t total_row_number = carrier->GetTableRowNumber();
    if (preview_data_size > total_row_number) {
        preview_data_size = total_row_number;
    }
    open_file_response.set_data_size(preview_data_size);

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
    std::unique_lock<std::mutex> lock(_carriers_mutex);
    if (!_carriers.count(file_id)) {
        std::cerr << "VOTable file does not exist (file ID: " << file_id << "!" << std::endl;
        return;
    }
    _carriers[file_id]->GetFilteredData(
        filter_request, [&](CARTA::CatalogFilterResponse filter_response) { partial_results_callback(filter_response); });
}

std::string Controller::GetCurrentWorkingPath() {
    char buff[FILENAME_MAX];
    getcwd(buff, FILENAME_MAX);
    std::string current_working_path(buff);
    return current_working_path;
}

std::string Controller::GetFileSize(std::string file_path_name) {
    struct stat file_status;
    stat(file_path_name.c_str(), &file_status);
    return (std::to_string(file_status.st_size) + " (bytes)");
}

int Controller::GetFileKBSize(std::string file_path_name) {
    struct stat file_status;
    stat(file_path_name.c_str(), &file_status);
    return (std::round((float)file_status.st_size / 1000));
}

void Controller::ParseBasePath(std::string& file_path_name) {
    std::string base_path("$BASE");
    if (file_path_name.find(base_path) != std::string::npos) {
        std::string current_working_path = GetCurrentWorkingPath();
        file_path_name.replace(file_path_name.find(base_path), base_path.length(), current_working_path);
    }
}

std::string Controller::Concatenate(std::string directory, std::string filename) {
    std::string file_path_name;
    if (directory.empty()) {
        file_path_name = filename;
    } else {
        file_path_name = directory + "/" + filename;
    }
    ParseBasePath(file_path_name);

    return file_path_name;
}

void Controller::CloseFile(int file_id) {
    std::unique_lock<std::mutex> lock(_carriers_mutex);
    if (_carriers.count(file_id)) {
        delete _carriers[file_id];
        _carriers[file_id] = nullptr;
        _carriers.erase(file_id);
    }
}