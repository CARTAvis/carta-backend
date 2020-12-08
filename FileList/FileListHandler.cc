/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "FileListHandler.h"

#include <casacore/casa/OS/DirectoryIterator.h>
#include <casacore/casa/OS/File.h>

#include "FileInfoLoader.h"

// Default constructor
FileListHandler::FileListHandler(const std::string& root, const std::string& base)
    : _root_folder(root), _base_folder(base), _filelist_folder("nofolder") {}

void FileListHandler::OnFileListRequest(const CARTA::FileListRequest& request, CARTA::FileListResponse& response, ResultMsg& result_msg) {
    // use tbb scoped lock so that it only processes the file list a time for one user
    // TODO: Do we still need a lock here if there are no API keys?
    std::scoped_lock lock(_file_list_mutex);
    string folder = request.directory();
    // do not process same directory simultaneously (e.g. double-click folder in browser)
    if (folder == _filelist_folder) {
        return;
    }

    _filelist_folder = folder;

    // resolve empty folder string or current dir "."
    if (folder.empty() || folder.compare(".") == 0) {
        folder = _root_folder;
    }

    // resolve $BASE keyword in folder string
    if (folder.find("$BASE") != std::string::npos) {
        casacore::String folder_string(folder);
        folder_string.gsub("$BASE", _base_folder);
        folder = folder_string;
    }
    // strip root_folder from folder
    GetRelativePath(folder);

    // get file list response and result message if any
    GetFileList(response, folder, result_msg);

    _filelist_folder = "nofolder"; // ready for next file list request
}

void FileListHandler::GetRelativePath(std::string& folder) {
    // Remove root folder path from given folder string
    if (folder.find("./") == 0) {
        folder.replace(0, 2, ""); // remove leading "./"
    } else if (folder.find(_root_folder) == 0) {
        folder.replace(0, _root_folder.length(), ""); // remove root folder path
        if (folder.front() == '/') {
            folder.replace(0, 1, "");
        } // remove leading '/'
    }
    if (folder.empty()) {
        folder = ".";
    }
}

void FileListHandler::GetFileList(CARTA::FileListResponse& file_list, std::string folder, ResultMsg& result_msg, bool region_list) {
    // fill FileListResponse
    std::string requested_folder = ((folder.compare(".") == 0) ? _root_folder : folder);
    casacore::Path requested_path(_root_folder);
    if (requested_folder == _root_folder) {
        // set directory in response; parent is null
        file_list.set_directory(".");
    } else { // append folder to root folder
        requested_path.append(folder);
        // set directory and parent in response
        std::string parent_dir(requested_path.dirName());
        GetRelativePath(parent_dir);
        file_list.set_directory(folder);
        file_list.set_parent(parent_dir);
        try {
            requested_folder = requested_path.resolvedName();
        } catch (casacore::AipsError& err) {
            try {
                requested_folder = requested_path.absoluteName();
            } catch (casacore::AipsError& err) {
                file_list.set_success(false);
                file_list.set_message("Cannot resolve directory path.");
                return;
            }
        }
    }
    casacore::File folder_path(requested_folder);
    std::string message;

    try {
        if (!folder_path.exists()) {
            file_list.set_success(false);
            file_list.set_message("Requested directory " + folder + " does not exist.");
            return;
        }

        if (!folder_path.isDirectory()) {
            file_list.set_success(false);
            file_list.set_message("Requested path " + folder + " is not a directory.");
            return;
        }

        // Iterate through directory to generate file list
        casacore::Directory start_dir(folder_path);
        casacore::DirectoryIterator dir_iter(start_dir);
        while (!dir_iter.pastEnd()) {
            casacore::File cc_file(dir_iter.file());          // directory is also a File
            casacore::String name(cc_file.path().baseName()); // to keep link name before resolve

            if (cc_file.isReadable() && cc_file.exists() && name.firstchar() != '.') { // ignore hidden files/folders
                casacore::String full_path(cc_file.path().absoluteName());
                try {
                    bool is_region(false);
                    if (region_list) {
                        if (casacore::ImageOpener::imageType(full_path) == casacore::ImageOpener::UNKNOWN) { // not image
                            if (cc_file.isRegular(true) && cc_file.isReadable()) {
                                CARTA::FileType file_type(GetRegionType(full_path));
                                if (file_type != CARTA::FileType::UNKNOWN) {
                                    auto& file_info = *file_list.add_files();
                                    FillRegionFileInfo(file_info, full_path, file_type);
                                    is_region = true;
                                }
                            }
                        }
                    }
                    if (!is_region) {
                        bool add_image(false);
                        auto image_type = casacore::ImageOpener::imageType(full_path);
                        if (cc_file.isDirectory(true) && cc_file.isExecutable()) {
                            switch (image_type) {
                                case casacore::ImageOpener::AIPSPP:
                                case casacore::ImageOpener::MIRIAD:
                                case casacore::ImageOpener::IMAGECONCAT:
                                case casacore::ImageOpener::IMAGEEXPR:
                                case casacore::ImageOpener::COMPLISTIMAGE:
                                    add_image = true;
                                    break;
                                case casacore::ImageOpener::UNKNOWN: {
                                    // Check if it is a directory and the user has permission to access it
                                    casacore::String dir_name(cc_file.path().baseName());
                                    file_list.add_subdirectories(dir_name);
                                    break;
                                }
                                default: {
                                    std::string image_type_msg = fmt::format(
                                        "{}: image type {} not supported", cc_file.path().baseName(), GetCasacoreTypeString(image_type));
                                    result_msg = {image_type_msg, {"file_list"}, CARTA::ErrorSeverity::DEBUG};
                                    break;
                                }
                            }
                        } else if (cc_file.isRegular(true) && cc_file.isReadable()) {
                            if ((image_type == casacore::ImageOpener::FITS) || (image_type == casacore::ImageOpener::HDF5)) {
                                add_image = true;
                            } else if (region_list) { // list unknown files: name, type, size
                                add_image = true;
                            }
                        }

                        if (add_image) { // add image to file list
                            auto& file_info = *file_list.add_files();
                            file_info.set_name(name);
                            FillFileInfo(file_info, full_path);
                        }
                    }
                } catch (casacore::AipsError& err) { // RegularFileIO error
                    // skip it
                }
            }
            dir_iter++;
        }
    } catch (casacore::AipsError& err) {
        result_msg = {err.getMesg(), {"file-list"}, CARTA::ErrorSeverity::ERROR};
        file_list.set_success(false);
        file_list.set_message(err.getMesg());
        return;
    }
    file_list.set_success(true);
}

std::string FileListHandler::GetCasacoreTypeString(casacore::ImageOpener::ImageTypes type) { // convert enum to string
    std::string type_str;
    switch (type) {
        case casacore::ImageOpener::GIPSY:
            type_str = "Gipsy";
            break;
        case casacore::ImageOpener::CAIPS:
            type_str = "Classic AIPS";
            break;
        case casacore::ImageOpener::NEWSTAR:
            type_str = "Newstar";
            break;
        case casacore::ImageOpener::IMAGECONCAT:
            type_str = "ImageConcat";
            break;
        case casacore::ImageOpener::IMAGEEXPR:
            type_str = "ImageExpr";
            break;
        case casacore::ImageOpener::COMPLISTIMAGE:
            type_str = "ComponentListImage";
            break;
        default:
            type_str = "Unknown";
            break;
    }
    return type_str;
}

bool FileListHandler::FillFileInfo(CARTA::FileInfo& file_info, const string& filename) {
    // fill FileInfo submessage
    FileInfoLoader info_loader = FileInfoLoader(filename);
    return info_loader.FillFileInfo(file_info);
}

void FileListHandler::OnRegionListRequest(
    const CARTA::RegionListRequest& region_request, CARTA::RegionListResponse& region_response, ResultMsg& result_msg) {
    // use tbb scoped lock so that it only processes the file list a time for one user
    std::scoped_lock lock(_region_list_mutex);
    string folder = region_request.directory();
    // do not process same directory simultaneously (e.g. double-click folder in browser)
    if (folder == _regionlist_folder) {
        return;
    }

    _regionlist_folder = folder;

    // resolve empty folder string or current dir "."
    if (folder.empty() || folder.compare(".") == 0) {
        folder = _root_folder;
    }

    // resolve $BASE keyword in folder string
    if (folder.find("$BASE") != std::string::npos) {
        casacore::String folder_string(folder);
        folder_string.gsub("$BASE", _base_folder);
        folder = folder_string;
    }
    // strip root_folder from folder
    GetRelativePath(folder);

    // get file list response and result message if any
    CARTA::FileListResponse file_response;
    GetFileList(file_response, folder, result_msg, true);
    // copy to region list message
    region_response.set_success(file_response.success());
    region_response.set_message(file_response.message());
    region_response.set_directory(file_response.directory());
    region_response.set_parent(file_response.parent());
    *region_response.mutable_files() = {file_response.files().begin(), file_response.files().end()};
    *region_response.mutable_subdirectories() = {file_response.subdirectories().begin(), file_response.subdirectories().end()};

    _regionlist_folder = "nofolder"; // ready for next file list request
}

CARTA::FileType FileListHandler::GetRegionType(const std::string& filename) {
    // Check beginning of file for CRTF or REG header
    CARTA::FileType file_type(CARTA::FileType::UNKNOWN);
    std::ifstream region_file(filename);
    try {
        std::string first_line;
        if (!region_file.eof()) { // empty file
            getline(region_file, first_line);
        }
        region_file.close();
        if (first_line.find("#CRTF") == 0) {
            file_type = CARTA::FileType::CRTF;
        } else if (first_line.find("# Region file format: DS9") == 0) { // optional header, but what else to do?
            file_type = CARTA::FileType::DS9_REG;
        }
    } catch (std::ios_base::failure& f) {
        region_file.close();
    }
    return file_type;
}

bool FileListHandler::FillRegionFileInfo(CARTA::FileInfo& file_info, const std::string& filename, CARTA::FileType type) {
    // For region list and info response: name, type, size
    casacore::File cc_file(filename);
    if (!cc_file.exists()) {
        return false;
    }

    // name
    std::string filename_only = cc_file.path().baseName();
    file_info.set_name(filename_only);

    // FileType
    if (type == CARTA::FileType::UNKNOWN) { // not passed in
        type = GetRegionType(filename);
    }
    file_info.set_type(type);

    // size
    int64_t file_size(cc_file.size());
    if (cc_file.isSymLink()) { // get size of file not size of link
        casacore::String resolved_filename(cc_file.path().resolvedName());
        casacore::File linked_file(resolved_filename);
        file_size = linked_file.size();
    }
    file_info.set_size(file_size);
    file_info.set_date(cc_file.modifyTime());
    file_info.add_hdu_list("");
    return true;
}

void FileListHandler::OnRegionFileInfoRequest(
    const CARTA::RegionFileInfoRequest& request, CARTA::RegionFileInfoResponse& response, ResultMsg& result_msg) {
    // Fill response message with file info and contents
    casacore::Path root_path(_root_folder);
    root_path.append(request.directory());
    auto filename = request.file();
    root_path.append(filename);
    casacore::File cc_file(root_path);
    std::string message, contents;
    bool success(false);

    if (!cc_file.exists()) {
        message = "File " + filename + " does not exist.";
        response.add_contents(contents);
    } else if (!cc_file.isRegular(true)) {
        message = "File " + filename + " is not a region file.";
        response.add_contents(contents);
    } else if (!cc_file.isReadable()) {
        message = "File " + filename + " is not readable.";
        response.add_contents(contents);
    } else {
        casacore::String full_name(cc_file.path().resolvedName());
        auto& file_info = *response.mutable_file_info();
        FillRegionFileInfo(file_info, full_name);
        std::vector<std::string> file_contents;
        if (file_info.type() == CARTA::FileType::UNKNOWN) {
            message = "File " + filename + " is not a region file.";
            response.add_contents(contents);
        } else {
            GetRegionFileContents(full_name, file_contents);
            success = true;
            *response.mutable_contents() = {file_contents.begin(), file_contents.end()};
        }
    }
    response.set_success(success);
    response.set_message(message);
}

void FileListHandler::GetRegionFileContents(std::string& full_name, std::vector<std::string>& file_contents) {
    // read each line of file into string in vector
    std::ifstream region_file(full_name);
    try {
        if (!region_file) {
            return;
        }

        std::string file_line;
        while (!region_file.eof()) {
            getline(region_file, file_line);
            file_contents.push_back(file_line);
        }
        region_file.close();
    } catch (std::ios_base::failure& f) {
        region_file.close();
    }
}
