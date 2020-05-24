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
    if (!GetAbsBasePath(directory)) {
        return;
    }

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
    }

    // Fill the file list response
    file_list_response.set_success(success);
    file_list_response.set_message(message);

    if (!success) {
        return;
    }

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

    // Parse the relative path to the absolute path
    if (!GetAbsBasePath(directory)) {
        return;
    }

    std::string filename(file_info_request.name());
    std::string file_path_name = Concatenate(directory, filename);

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

bool Controller::GetAbsBasePath(std::string& directory) {
    bool success(false);
    std::string root_folder = _root_folder;
    root_folder.replace(0, 1, ""); // Remove "/" at the start of root path name
    if (!root_folder.empty() && (directory.find(root_folder) == std::string::npos) && (directory.find("../") == std::string::npos)) {
        // For the path name "base/path/images", change to "/root/path/base/path/images"
        directory = _root_folder + "/" + directory;
        success = true;
    } else if (root_folder.empty() || (!root_folder.empty() && (directory.find(root_folder) == 0))) {
        // For the path "root/path/base/path/images", change to "/root/path/base/path/images"
        directory = "/" + directory;
        success = true;
    } else {
        std::cerr << "Unknown directory: " << directory << "!\n";
    }
    return success;
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