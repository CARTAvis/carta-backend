#include "FileListHandler.h"
#include "FileInfoLoader.h"

#include <casacore/casa/OS/DirectoryIterator.h>

// Default constructor
FileListHandler::FileListHandler(std::unordered_map<std::string,
    std::vector<std::string>>& permissionsMap, bool enforcePermissions,
    std::string root, std::string base)
    : permissionsMap(permissionsMap),
      permissionsEnabled(enforcePermissions),
      rootFolder(root),
      baseFolder(base),
      filelistFolder("nofolder") {
}

FileListHandler::~FileListHandler() {
}

void FileListHandler::onFileListRequest(const CARTA::FileListRequest& request,
    uint32_t requestId, CARTA::FileListResponse &response, ResultMsg& resultMsg) {
    // use tbb scoped lock so that it only processes the file list a time for one user
    tbb::mutex::scoped_lock lock(fileListMutex);
    string folder = request.directory();
    // do not process same directory simultaneously (e.g. double-click folder in browser)
    if (folder == filelistFolder) {
        return;
    } else {
        filelistFolder = folder;
    }

    // resolve empty folder string or current dir "."
    if (folder.empty() || folder.compare(".")==0)
        folder = rootFolder;
    // resolve $BASE keyword in folder string
    if (folder.find("$BASE") != std::string::npos) {
        casacore::String folderString(folder);
        folderString.gsub("$BASE", baseFolder);
        folder = folderString;
    }
    // strip rootFolder from folder
    getRelativePath(folder);

    // get file list response and result message if any
    getFileList(response, folder, resultMsg);

    filelistFolder = "nofolder";  // ready for next file list request
}

void FileListHandler::getRelativePath(std::string& folder) {
    // Remove root folder path from given folder string
    if (folder.find("./")==0) {
        folder.replace(0, 2, ""); // remove leading "./"
    } else if (folder.find(rootFolder)==0) {
        folder.replace(0, rootFolder.length(), ""); // remove root folder path
        if (folder.front()=='/') folder.replace(0,1,""); // remove leading '/'
    }
    if (folder.empty()) folder=".";
}

void FileListHandler::getFileList(CARTA::FileListResponse& fileList, string folder, ResultMsg& resultMsg) {
    // fill FileListResponse
    std::string requestedFolder = ((folder.compare(".")==0) ? rootFolder : folder);
    casacore::Path requestedPath(rootFolder);
    if (requestedFolder == rootFolder) {
        // set directory in response; parent is null
        fileList.set_directory(".");
    } else  { // append folder to root folder
        casacore::Path requestedPath(rootFolder);
        requestedPath.append(folder);
        // set directory and parent in response
        std::string parentDir(requestedPath.dirName());
        getRelativePath(parentDir);
        fileList.set_directory(folder);
        fileList.set_parent(parentDir);
        try {
            requestedFolder = requestedPath.resolvedName();
        } catch (casacore::AipsError& err) {
            try {
                requestedFolder = requestedPath.absoluteName();
            } catch (casacore::AipsError& err) {
                fileList.set_success(false);
                fileList.set_message("Cannot resolve directory path.");
                return;
            }
        }
    }
    casacore::File folderPath(requestedFolder);
    string message;

    try {
        if (folderPath.exists() && folderPath.isDirectory() && checkPermissionForDirectory(folder)) {
            // Iterate through directory to generate file list
            casacore::Directory startDir(folderPath);
            casacore::DirectoryIterator dirIter(startDir);
            while (!dirIter.pastEnd()) {
                casacore::File ccfile(dirIter.file());  // directory is also a File
                casacore::String name(ccfile.path().baseName()); // in case it is a link
                if (ccfile.exists() && name.firstchar() != '.') {  // ignore hidden files/folders
                    casacore::String fullpath(ccfile.path().absoluteName());
                    try {
                        bool addImage(false);
                        if (ccfile.isDirectory(true) && ccfile.isExecutable() && ccfile.isReadable()) {
                            casacore::ImageOpener::ImageTypes imType = casacore::ImageOpener::imageType(fullpath);
                            if ((imType==casacore::ImageOpener::AIPSPP) || (imType==casacore::ImageOpener::MIRIAD))
                                addImage = true;
                            else if (imType==casacore::ImageOpener::UNKNOWN) {
                                // Check if it is a directory and the user has permission to access it
                                casacore::String dirname(ccfile.path().baseName());
                                string pathNameRelative = (folder.length() && folder != "/") ? folder + "/" + string(dirname): dirname;
                                if (checkPermissionForDirectory(pathNameRelative))
                                   fileList.add_subdirectories(dirname);
                            } else {
                                std::string imageTypeMsg = fmt::format("{}: image type {} not supported", ccfile.path().baseName(), getType(imType));
                                resultMsg = {imageTypeMsg, {"file_list"}, CARTA::ErrorSeverity::DEBUG};
                            }
                        } else if (ccfile.isRegular(true) && ccfile.isReadable()) {
                            casacore::ImageOpener::ImageTypes imType = casacore::ImageOpener::imageType(fullpath);
                            if ((imType==casacore::ImageOpener::FITS) || (imType==casacore::ImageOpener::HDF5))
                                addImage = true;
                        }

                        if (addImage) { // add image to file list
                            auto fileInfo = fileList.add_files();
                            fileInfo->set_name(name);
                            bool ok = fillFileInfo(fileInfo, fullpath);
                        }
                    } catch (casacore::AipsError& err) {  // RegularFileIO error
                        // skip it
                    }
                }
                dirIter++;
            }
        } else {
            fileList.set_success(false);
            fileList.set_message("Cannot read directory; check name and permissions.");
            return;
        }
    } catch (casacore::AipsError& err) {
        resultMsg = {err.getMesg(), {"file-list"}, CARTA::ErrorSeverity::ERROR};
        fileList.set_success(false);
        fileList.set_message(err.getMesg());
        return;
    }
    fileList.set_success(true);
}

// Checks whether the user's API key is valid for a particular directory.
// This function is called recursively, starting with the requested directory, and then working
// its way up parent directories until it finds a matching directory in the permissions map.
bool FileListHandler::checkPermissionForDirectory(std::string prefix) {
    // skip permissions map if we're not running with permissions enabled
    if (!permissionsEnabled) {
        return true;
    }

    // trim leading dot
    if (prefix.length() && prefix[0] == '.') {
        prefix.erase(0, 1);
    }
    // Check for root folder permissions
    if (!prefix.length() || prefix == "/") {
        if (permissionsMap.count("/")) {
            return checkPermissionForEntry("/");
        }
        return false;
    } else {
        // trim trailing and leading slash
        if (prefix[prefix.length() - 1] == '/') {
            prefix = prefix.substr(0, prefix.length() - 1);
        }
        if (prefix[0] == '/') {
            prefix = prefix.substr(1);
        }
        while (prefix.length() > 0) {
            if (permissionsMap.count(prefix)) {
                return checkPermissionForEntry(prefix);
            }
            auto lastSlash = prefix.find_last_of('/');

            if (lastSlash == string::npos) {
                return false;
            } else {
                prefix = prefix.substr(0, lastSlash);
            }
        }
        return false;
    }
}

bool FileListHandler::checkPermissionForEntry(string entry) {
    // skip permissions map if we're not running with permissions enabled
    if (!permissionsEnabled) {
        return true;
    }
    if (!permissionsMap.count(entry)) {
        return false;
    }
    auto& keys = permissionsMap[entry];
    return (find(keys.begin(), keys.end(), "*") != keys.end()) || (find(keys.begin(), keys.end(), apiKey) != keys.end());
}

std::string FileListHandler::getType(casacore::ImageOpener::ImageTypes type) { // convert enum to string
    std::string typeStr;
    switch(type) {
        case casacore::ImageOpener::GIPSY:
            typeStr = "Gipsy";
            break;
        case casacore::ImageOpener::CAIPS:
            typeStr = "Classic AIPS";
            break;
        case casacore::ImageOpener::NEWSTAR:
            typeStr = "Newstar";
            break;
        case casacore::ImageOpener::IMAGECONCAT:
            typeStr = "ImageConcat";
            break;
        case casacore::ImageOpener::IMAGEEXPR:
            typeStr = "ImageExpr";
            break;
        case casacore::ImageOpener::COMPLISTIMAGE:
            typeStr = "ComponentListImage";
            break;
        default:
            typeStr = "Unknown";
            break;
    }
    return typeStr;
}

bool FileListHandler::fillFileInfo(CARTA::FileInfo* fileInfo, const string& filename) {
    // fill FileInfo submessage
    FileInfoLoader infoLoader(filename);
    return infoLoader.fillFileInfo(fileInfo);
}
