#include "VOTableController.h"

#include <dirent.h>
#include <stdio.h> /* defines the FILENAME_MAX */
#include <sys/stat.h>
#include <unistd.h> // Defines the function getcwd(...)

#include "VOTableCarrier.h"
#include "VOTableParser.h"

using namespace catalog;

Controller::~Controller() {
    for (auto& carrier : _carriers) {
        delete carrier.second;
        carrier.second = nullptr;
    }
    _carriers.clear();
}

void Controller::OnFileListRequest(FileListRequest file_list_request, FileListResponse& file_list_response) {
    bool success(false);
    std::string message;
    std::string directory(file_list_request.directory);

    // Replace the $BASE with current working path
    ParseBasePath(directory);

    // Get a list of files under the directory
    DIR* current_path;
    struct dirent* current_entry;
    if ((current_path = opendir(directory.c_str()))) {
        success = true; // The directory exists
        while ((current_entry = readdir(current_path))) {
            if (strcmp(current_entry->d_name, ".") != 0 && strcmp(current_entry->d_name, "..") != 0) {
                std::string tmp_name = current_entry->d_name;
                // Check is it a XML file
                if (tmp_name.substr(tmp_name.find_last_of(".") + 1) == "xml") {
                    // Check is it a VOTable XML file
                    std::string tmp_path_name = Concatenate(directory, tmp_name);
                    if (VOTableParser::IsVOTable(tmp_path_name)) {
                        // Get the file size
                        std::string tmp_file_description = GetFileSize(tmp_path_name);
                        // Fill the file info
                        FileInfo tmp_file_info;
                        tmp_file_info.filename = tmp_name;
                        tmp_file_info.file_type = VOTable;
                        tmp_file_info.description = tmp_file_description;
                        file_list_response.files.push_back(tmp_file_info);
                    }
                } else {
                    // Check is it a sub-directory
                    DIR* sub_path;
                    std::string sub_directory = Concatenate(directory, current_entry->d_name);
                    if ((sub_path = opendir(sub_directory.c_str()))) {
                        file_list_response.subdirectories.push_back(sub_directory);
                    }
                    closedir(sub_path);
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
    file_list_response.success = success;
    file_list_response.message = message;
    file_list_response.directory = directory;
    file_list_response.parent = parent_directory;
}

void Controller::OnFileInfoRequest(FileInfoRequest file_info_request, FileInfoResponse& file_info_response) {
    bool success(false);
    std::string message;
    std::string directory(file_info_request.directory);
    std::string filename(file_info_request.filename);
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
    file_info_response.success = success;
    file_info_response.message = message;
    file_info_response.file_info.filename = filename;
    file_info_response.file_info.file_type = VOTable;
    file_info_response.file_info.description = GetFileSize(file_path_name);
    carrier.GetHeaders(file_info_response);
    file_info_response.data_size =
        carrier.GetTableRowNumber(); // TODO: since we only read the headers, we don't know the number of table rows
}

void Controller::OnOpenFileRequest(OpenFileRequest open_file_request, OpenFileResponse& open_file_response) {
    bool success(false);
    std::string message;
    std::string directory(open_file_request.directory);
    std::string filename(open_file_request.filename);
    std::string file_path_name = Concatenate(directory, filename);

    int file_id(open_file_request.file_id);
    int preview_data_size(open_file_request.preview_data_size);
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
    open_file_response.success = success;
    open_file_response.message = message;
    open_file_response.file_id = file_id;
    open_file_response.file_info.filename = filename;
    open_file_response.file_info.file_type = VOTable;
    open_file_response.file_info.description = GetFileSize(file_path_name);

    // Fill the number of raws
    size_t total_row_number = carrier->GetTableRowNumber();
    if (preview_data_size > total_row_number) {
        preview_data_size = total_row_number;
    }
    open_file_response.data_size = preview_data_size;

    // Fill the table headers and their columns data
    carrier->GetHeadersAndData(open_file_response, preview_data_size);

    // Move the VOTableCarrier with respect to its file_id to the cache
    CloseFile(file_id);
    _carriers[file_id] = std::move(carrier);
}

void Controller::OnCloseFileRequest(CloseFileRequest close_file_request) {
    int file_id(close_file_request.file_id);
    CloseFile(file_id);
}

void Controller::OnFilterRequest(FilterRequest filter_request, std::function<void(FilterResponse)> partial_results_callback) {
    int file_id(filter_request.file_id);
    if (!_carriers.count(file_id)) {
        std::cerr << "VOTable file does not exist (file ID: " << file_id << "!" << std::endl;
        return;
    }
    _carriers[file_id]->GetFilteredData(filter_request, [&](FilterResponse filter_response) { partial_results_callback(filter_response); });
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
    if (_carriers.count(file_id)) {
        delete _carriers[file_id];
        _carriers[file_id] = nullptr;
        _carriers.erase(file_id);
    }
}