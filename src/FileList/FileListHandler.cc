/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
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
#include "Region/Ds9ImportExport.h"
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

    // resolve empty folder string or current dir "."
    if (folder.empty() || folder.compare(".") == 0) {
        folder = _top_level_folder;
    }

    // resolve $BASE keyword in folder string
    if (folder.find("$BASE") != std::string::npos) {
        casacore::String folder_string(folder);
        folder_string.gsub("$BASE", _starting_folder);
        folder = folder_string;
    }
    // strip root_folder from folder
    GetRelativePath(folder);

    // get file list response and result message if any
    GetFileList(response, folder, result_msg, request.filter_mode());

    _filelist_folder = "nofolder"; // ready for next file list request
}

void FileListHandler::GetRelativePath(std::string& folder) {
    // Remove root folder path from given folder string
    if (folder.find("./") == 0) {
        folder.replace(0, 2, ""); // remove leading "./"
    } else if (folder.find(_top_level_folder) == 0) {
        folder.replace(0, _top_level_folder.length(), ""); // remove root folder path
        if (folder.front() == '/') {
            folder.replace(0, 1, "");
        } // remove leading '/'
    }
    if (folder.empty()) {
        folder = ".";
    }
}

void FileListHandler::GetFileList(CARTA::FileListResponse& file_list, std::string folder, ResultMsg& result_msg,
    CARTA::FileListFilterMode filter_mode, bool region_list) {
    // fill FileListResponse
    std::string requested_folder = ((folder.compare(".") == 0) ? _top_level_folder : folder);
    casacore::Path requested_path(_top_level_folder);
    if (requested_folder == _top_level_folder) {
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

    if ((_top_level_folder.find(requested_folder) == 0) && (requested_folder.length() < _top_level_folder.length())) {
        file_list.set_success(false);
        file_list.set_message("Forbidden path.");
        return;
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

        // initialize variables for the progress report and the interruption option
        _stop_getting_file_list = false;
        _first_report_made = false;
        ListProgressReporter progress_reporter(start_dir.nEntries(), _progress_callback);

        while (!dir_iter.pastEnd()) {
            if (_stop_getting_file_list) {
                file_list.set_cancel(true);
                break;
            }

            casacore::File cc_file(dir_iter.file());          // directory is also a File
            casacore::String name(cc_file.path().baseName()); // to keep link name before resolve

            if (cc_file.isReadable() && cc_file.exists() && name.firstchar() != '.') { // ignore hidden files/folders
                casacore::String full_path(cc_file.path().absoluteName());
                try {
                    bool is_region_file(false);

                    if (region_list && cc_file.isRegular(true)) {
                        auto region_file_type = GuessRegionType(full_path, filter_mode == CARTA::Content);

                        // Try to parse file as DS9 unless it is an image file
                        if ((region_file_type == CARTA::FileType::UNKNOWN) && IsDs9FileNoHeader(full_path)) {
                            region_file_type = CARTA::FileType::DS9_REG;
                        }

                        if (region_file_type != CARTA::FileType::UNKNOWN || filter_mode == CARTA::AllFiles) {
                            auto& file_info = *file_list.add_files();
                            FillRegionFileInfo(file_info, full_path, region_file_type, false);
                            is_region_file = true; // Done with file
                        }
                    }

                    if (!is_region_file) {
                        // Whether to add to file list
                        bool add_file(false);
                        CARTA::FileType file_type(CARTA::FileType::UNKNOWN);

                        if (cc_file.isDirectory(true) && cc_file.isExecutable()) {
                            // Determine if image or directory
                            auto image_type = CasacoreImageType(full_path);
                            switch (image_type) {
                                case casacore::ImageOpener::AIPSPP:
                                case casacore::ImageOpener::IMAGECONCAT:
                                case casacore::ImageOpener::IMAGEEXPR:
                                case casacore::ImageOpener::COMPLISTIMAGE: {
                                    file_type = CARTA::FileType::CASA;
                                    add_file = true;
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
                                    add_file = true;
                                    break;
                                }
                                case casacore::ImageOpener::UNKNOWN: {
                                    // UNKNOWN directories are directories
                                    casacore::String dir_name(cc_file.path().baseName());
                                    auto directory_info = file_list.add_subdirectories();
                                    directory_info->set_name(dir_name);
                                    directory_info->set_date(cc_file.modifyTime());
                                    directory_info->set_item_count(GetNumItems(cc_file.path().absoluteName()));
                                    break;
                                }
                                default:
                                    break;
                            }
                        } else if (!region_list && cc_file.isRegular(true)) {
                            file_type = GuessImageType(full_path, filter_mode == CARTA::Content);
                            add_file = filter_mode == CARTA::AllFiles || file_type != CARTA::UNKNOWN;
                        }

                        if (add_file) { // add to file list: name, type, size, date
                            auto& file_info = *file_list.add_files();
                            file_info.set_name(name);
                            FileInfoLoader info_loader = FileInfoLoader(full_path, file_type);
                            info_loader.FillFileInfo(file_info);
                        }
                    }
                } catch (casacore::AipsError& err) { // RegularFileIO error
                    // skip it
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
        file_list.set_success(false);
        file_list.set_message(err.getMesg());
        return;
    }

    file_list.set_success(true);
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

    // resolve empty folder string or current dir "."
    if (folder.empty() || folder.compare(".") == 0) {
        folder = _top_level_folder;
    }

    // resolve $BASE keyword in folder string
    if (folder.find("$BASE") != std::string::npos) {
        casacore::String folder_string(folder);
        folder_string.gsub("$BASE", _starting_folder);
        folder = folder_string;
    }
    // strip root_folder from folder
    GetRelativePath(folder);

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

bool FileListHandler::IsDs9FileNoHeader(const std::string& full_path) {
    if (GuessImageType(full_path, true) != CARTA::FileType::UNKNOWN) {
        return false;
    }

    try {
        std::shared_ptr<casacore::CoordinateSystem> coord_sys(nullptr);
        casacore::IPosition shape;
        auto ds9_importer = Ds9ImportExport(coord_sys, shape, -1, full_path, true);
        return true;
    } catch (const casacore::AipsError& err) {
        // no DS9 regions found
    }

    return false;
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
        // not passed in, check magic number
        type = GuessRegionType(filename, true);

        if ((type == CARTA::FileType::UNKNOWN) && IsDs9FileNoHeader(filename)) {
            type = CARTA::FileType::DS9_REG;
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
    casacore::Path root_path(_top_level_folder);
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
