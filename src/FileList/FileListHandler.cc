/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2024 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "FileListHandler.h"

#include <fstream>

#include <spdlog/fmt/fmt.h>

#include <casacore/casa/Exceptions/Error.h>
#include <casacore/casa/OS/DirectoryIterator.h>
#include <casacore/casa/OS/File.h>

#include "../Logger/Logger.h"
#include "FileInfoLoader.h"
#include "Timer/ListProgressReporter.h"
#include "Util/Casacore.h"
#include "Util/File.h"

using namespace carta;

// Default constructor
FileListHandler::FileListHandler(const std::string& top_level_folder, const std::string& starting_folder)
    : _top_level_folder(top_level_folder), _starting_folder(starting_folder), _filelist_folder("nofolder") {}

void FileListHandler::OnFileListRequest(const CARTA::FileListRequest& request, CARTA::FileListResponse& response, ResultMsg& result_msg) {
    // use scoped lock so that it only processes the file list a time for one user
    // TODO: Do we still need a lock here if there are no API keys?
    std::scoped_lock lock(_file_list_mutex);
    std::string folder = request.directory();
    // do not process same directory simultaneously (e.g. double-click folder in browser)
    if (folder == _filelist_folder) {
        return;
    }

    _filelist_folder = folder;

    // get file list response and result message if any
    GetFileList(response, folder, result_msg, request.filter_mode());

    _filelist_folder = "nofolder"; // ready for next file list request
}

void FileListHandler::GetRelativePath(std::string& folder) {
    // Remove top folder path from given folder string
    if (folder.find("./") == 0) {
        folder = folder.substr(2); // remove leading "./"
    } else if (folder.find(_top_level_folder) == 0) {
        folder = folder.substr(_top_level_folder.length()); // remove top folder path

        if (!folder.empty() && folder.front() == '/') {
            folder = folder.substr(1); // remove leading '/'
        }
    }
    if (folder.empty()) {
        folder = ".";
    }
}

void FileListHandler::GetFileList(CARTA::FileListResponse& file_list_response, const std::string& folder, ResultMsg& result_msg,
    CARTA::FileListFilterMode filter_mode, bool region_list) {
    // Fill FileListResponse message for folder (or folder path).
    std::string requested_folder(folder);

    if (requested_folder.empty() || requested_folder.compare(".") == 0) {
        // Resolve empty folder or current dir "." to top folder
        requested_folder = _top_level_folder;
    } else if (requested_folder.find("$BASE") != std::string::npos) {
        // Resolve $BASE keyword to starting folder
        casacore::String folder_string(requested_folder);
        folder_string.gsub("$BASE", _starting_folder);
        requested_folder = folder_string;
    }

    // Normalize requested folder relative to top (top + requested = full path)
    GetRelativePath(requested_folder);

    // Resolve path (., .., ~, symlinks)
    std::string message;
    auto resolved_path = GetResolvedFilename(_top_level_folder, requested_folder, "", message);

    // Check resolved path
    if (resolved_path.empty()) {
        file_list_response.set_success(false);
        file_list_response.set_message("File list failed: " + message);
        return;
    } else if ((_top_level_folder.find(resolved_path) == 0) && (resolved_path.length() < _top_level_folder.length())) {
        // path is above top folder!
        file_list_response.set_success(false);
        file_list_response.set_message("Forbidden path.");
        return;
    }

    casacore::File folder_path(resolved_path);
    if (!folder_path.isDirectory()) {
        file_list_response.set_success(false);
        file_list_response.set_message("File list failed: requested path " + folder + " is not a directory.");
        return;
    }

    // Set response parent and directory
    if (requested_folder == ".") {
        // is top folder; no directory
        file_list_response.set_parent(requested_folder);
    } else {
        // Make full path to separate directory and base names
        casacore::Path full_path(_top_level_folder);
        full_path.append(requested_folder);
        // parent
        std::string parent(full_path.dirName());
        GetRelativePath(parent);
        file_list_response.set_parent(parent);
        // directory
        std::string directory(full_path.baseName());
        file_list_response.set_directory(requested_folder);
    }

    // Iterate through directory to generate file list
    try {
        casacore::Directory start_dir(folder_path);
        casacore::DirectoryIterator dir_iter(start_dir);

        // initialize variables for the progress report and the interruption option
        _stop_getting_file_list = false;
        _first_report_made = false;
        ListProgressReporter progress_reporter(start_dir.nEntries(), _progress_callback);

        bool list_all_files(filter_mode == CARTA::AllFiles);

        while (!dir_iter.pastEnd()) {
            if (_stop_getting_file_list) {
                file_list_response.set_cancel(true);
                break;
            }

            casacore::File cc_file(dir_iter.file());          // directory is also a File
            casacore::String name(cc_file.path().baseName()); // to keep link name before resolve

            if (cc_file.isReadable() && cc_file.exists() && name.firstchar() != '.') { // ignore hidden files/folders
                casacore::String full_path(cc_file.path().absoluteName());

                try {
                    if (region_list) {
                        if (cc_file.isRegular(true)) {
                            auto file_type = GuessRegionType(full_path, filter_mode == CARTA::Content);

                            if (!list_all_files && file_type == CARTA::UNKNOWN) {
                                // Contents did not work, check extension (e.g. DS9 with no header)
                                file_type = GuessRegionType(full_path, false);
                            }

                            if (list_all_files || file_type != CARTA::UNKNOWN) {
                                // Add file: known region file, or not checking type
                                auto& file_info = *file_list_response.add_files();
                                FillRegionFileInfo(file_info, full_path, file_type, false);
                            }
                        } else if (cc_file.isDirectory(true) && cc_file.isExecutable() &&
                                   (list_all_files || CasacoreImageType(full_path) == casacore::ImageOpener::UNKNOWN)) {
                            // Add directory: not image if checking type, or not checking type
                            casacore::String dir_name(cc_file.path().baseName());
                            auto directory_info = file_list_response.add_subdirectories();
                            directory_info->set_name(dir_name);
                            directory_info->set_date(cc_file.modifyTime());
                            directory_info->set_item_count(GetNumItems(cc_file.path().absoluteName()));
                        }
                    } else {
                        // Image list
                        bool add_image_file(false);
                        CARTA::FileType file_type(CARTA::FileType::UNKNOWN);

                        if (cc_file.isDirectory(true) && cc_file.isExecutable()) {
                            // Determine if image or directory for image list
                            auto image_type = CasacoreImageType(full_path);

                            switch (image_type) {
                                case casacore::ImageOpener::AIPSPP:
                                case casacore::ImageOpener::IMAGECONCAT:
                                case casacore::ImageOpener::IMAGEEXPR:
                                case casacore::ImageOpener::COMPLISTIMAGE: {
                                    file_type = CARTA::FileType::CASA;
                                    add_image_file = true;
                                    break;
                                }
                                case casacore::ImageOpener::GIPSY:
                                case casacore::ImageOpener::CAIPS:
                                case casacore::ImageOpener::NEWSTAR: {
                                    std::string image_type_msg = fmt::format("{}: image type not supported", name);
                                    result_msg = {image_type_msg, {"file_list"}, CARTA::ErrorSeverity::DEBUG};
                                    break;
                                }
                                case casacore::ImageOpener::MIRIAD: {
                                    file_type = CARTA::FileType::MIRIAD;
                                    add_image_file = true;
                                    break;
                                }
                                case casacore::ImageOpener::UNKNOWN: {
                                    // UNKNOWN directories are directories
                                    casacore::String dir_name(cc_file.path().baseName());
                                    auto directory_info = file_list_response.add_subdirectories();
                                    directory_info->set_name(dir_name);
                                    directory_info->set_date(cc_file.modifyTime());
                                    directory_info->set_item_count(GetNumItems(cc_file.path().absoluteName()));
                                    break;
                                }
                                default:
                                    break;
                            }
                        } else if (cc_file.isRegular(true)) {
                            file_type = GuessImageType(full_path, filter_mode == CARTA::Content);
                            // Add file: known image file, or not checking type
                            add_image_file = list_all_files || file_type != CARTA::UNKNOWN;
                        }

                        if (add_image_file) {
                            auto& file_info = *file_list_response.add_files();
                            file_info.set_name(name);
                            FileInfoLoader info_loader = FileInfoLoader(full_path, file_type);
                            info_loader.FillFileInfo(file_info);
                        }
                    }
                } catch (casacore::AipsError& err) {
                    // RegularFileIO error, skip item
                }
            }

            dir_iter++;

            // update the progress and get the difference between the current time and start time
            auto dt = progress_reporter.UpdateProgress();

            // report the progress if it fits the conditions
            if (!_first_report_made && dt > FILE_LIST_FIRST_PROGRESS_AFTER_SECS) {
                progress_reporter.ReportFileListProgress(CARTA::FileListType::Image);
                _first_report_made = true;
            } else if (_first_report_made && dt > FILE_LIST_PROGRESS_INTERVAL_SECS) {
                progress_reporter.ReportFileListProgress(CARTA::FileListType::Image);
            }
        }
    } catch (casacore::AipsError& err) {
        result_msg = {err.getMesg(), {"file-list"}, CARTA::ErrorSeverity::ERROR};
        file_list_response.set_success(false);
        file_list_response.set_message(err.getMesg());
        return;
    }

    file_list_response.set_success(true);
}

void FileListHandler::OnRegionListRequest(
    const CARTA::RegionListRequest& region_request, CARTA::RegionListResponse& region_response, ResultMsg& result_msg) {
    // use scoped lock so that it only processes the file list a time for one user
    std::scoped_lock lock(_region_list_mutex);
    std::string folder = region_request.directory();
    // do not process same directory simultaneously (e.g. double-click folder in browser)
    if (folder == _regionlist_folder) {
        return;
    }

    _regionlist_folder = folder;

    // get file list response and result message if any
    CARTA::FileListResponse file_response;
    GetFileList(file_response, folder, result_msg, region_request.filter_mode(), true);

    // copy to region list message
    region_response.set_success(file_response.success());
    region_response.set_message(file_response.message());
    region_response.set_directory(file_response.directory());
    region_response.set_parent(file_response.parent());
    *region_response.mutable_files() = {file_response.files().begin(), file_response.files().end()};
    *region_response.mutable_subdirectories() = {file_response.subdirectories().begin(), file_response.subdirectories().end()};
    region_response.set_cancel(file_response.cancel());

    _regionlist_folder = "nofolder"; // ready for next file list request
}

bool FileListHandler::FillRegionFileInfo(
    CARTA::FileInfo& file_info, const std::string& filename, CARTA::FileType type, bool determine_file_type) {
    // For region list and info response: name, type, size
    casacore::File cc_file(filename);
    if (!cc_file.exists()) {
        return false;
    }

    // name
    std::string filename_only = cc_file.path().baseName();
    file_info.set_name(filename_only);

    // FileType
    if (type == CARTA::FileType::UNKNOWN && determine_file_type) {
        type = GuessRegionType(filename, true);

        if (type == CARTA::FileType::UNKNOWN) {
            type = GuessRegionType(filename, false);
        }
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
    auto directory = request.directory();
    auto file = request.file();
    std::string message;

    casacore::String full_name = GetResolvedFilename(_top_level_folder, directory, file, message);

    bool success(false), add_contents(true);
    if (!full_name.empty()) {
        casacore::File cc_file(full_name);
        if (!cc_file.isRegular(true)) {
            message = "File " + file + " is not a region file.";
        } else {
            auto& file_info = *response.mutable_file_info();
            FillRegionFileInfo(file_info, full_name);

            if (file_info.type() == CARTA::FileType::UNKNOWN) {
                message = "File " + file + " is not a region file.";
            } else {
                std::vector<std::string> file_contents;
                GetRegionFileContents(full_name, file_contents);
                success = true;
                *response.mutable_contents() = {file_contents.begin(), file_contents.end()};
                add_contents = false;
            }
        }
    }

    if (add_contents) {
        std::string contents;
        response.add_contents(contents);
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
