#include "Session.h"
#include "InterfaceConstants.h"
#include "FileInfoLoader.h"
#include "compression.h"
#include "util.h"
#include <carta-protobuf/error.pb.h>

#include <casacore/casa/OS/Path.h>
#include <casacore/casa/OS/DirectoryIterator.h>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include <chrono>
#include <limits>
#include <valarray>

// Default constructor. Associates a websocket with a UUID and sets the root and base folders for all files
Session::Session(uWS::WebSocket<uWS::SERVER>* ws, std::string uuid, std::unordered_map<string, 
    std::vector<std::string>>& permissionsMap, bool enforcePermissions, std::string root,
    std::string base, uS::Async *outgoing, bool verbose)
    : uuid(std::move(uuid)),
      socket(ws),
      permissionsMap(permissionsMap),
      permissionsEnabled(enforcePermissions),
      rootFolder(root),
      baseFolder(base),
      filelistFolder("nofolder"),
      verboseLogging(verbose),
      selectedFileInfo(nullptr),
      selectedFileInfoExtended(nullptr),
      outgoing(outgoing),
      newFrame(false) {
}

Session::~Session() {
    std::unique_lock<std::mutex> lock(frameMutex);
    for (auto& frame : frames) {
        frame.second.reset();  // delete Frame
    }
    frames.clear();
    outgoing->close();
}

bool Session::checkPermissionForEntry(string entry) {
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

// Checks whether the user's API key is valid for a particular directory.
// This function is called recursively, starting with the requested directory, and then working
// its way up parent directories until it finds a matching directory in the permissions map.
bool Session::checkPermissionForDirectory(std::string prefix) {
    // skip permissions map if we're not running with permissions enabled
    if (!permissionsEnabled) {
        return true;
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

// ********************************************************************************
// File browser

void Session::getRelativePath(std::string& folder) {
    // Remove root folder path from given folder string
    if (folder.find("./")==0) {
        folder.replace(0, 2, ""); // remove leading "./"
    } else if (folder.find(rootFolder)==0) {
        folder.replace(0, rootFolder.length(), ""); // remove root folder path
        if (folder.front()=='/') folder.replace(0,1,""); // remove leading '/'
    }
    if (folder.empty()) folder=".";
}

void Session::getFileList(CARTA::FileListResponse& fileList, string folder) {
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
                                string pathNameRelative = (folder.length() && folder != "/") ? folder.append("/" + dirname) : dirname;
                                if (checkPermissionForDirectory(pathNameRelative))
                                   fileList.add_subdirectories(dirname);
                            } else {
                                std::string imageTypeMsg = fmt::format("{}: image type {} not supported", ccfile.path().baseName(), getType(imType));
                                sendLogEvent(imageTypeMsg, {"file_list"}, CARTA::ErrorSeverity::DEBUG);
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
        sendLogEvent(err.getMesg(), {"file-list"}, CARTA::ErrorSeverity::ERROR);
        fileList.set_success(false);
        fileList.set_message(err.getMesg());
        return;
    }
    fileList.set_success(true);
}

std::string Session::getType(casacore::ImageOpener::ImageTypes type) { // convert enum to string
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

bool Session::fillFileInfo(CARTA::FileInfo* fileInfo, const string& filename) {
    // fill FileInfo submessage
    FileInfoLoader infoLoader(filename);
    return infoLoader.fillFileInfo(fileInfo);
}

bool Session::fillExtendedFileInfo(CARTA::FileInfoExtended* extendedInfo, CARTA::FileInfo* fileInfo, 
        const string folder, const string filename, string hdu, string& message) {
    // fill CARTA::FileInfoResponse submessages CARTA::FileInfo and CARTA::FileInfoExtended
    bool extFileInfoOK(true);
    try {
        fileInfo->set_name(filename); // in case filename is a link
        casacore::Path rootpath(rootFolder);
        rootpath.append(folder);
        rootpath.append(filename);
        casacore::File ccfile(rootpath);
        if (ccfile.exists()) {
            casacore::String fullname(ccfile.path().resolvedName());
            try {
                FileInfoLoader infoLoader(fullname);
                if (!infoLoader.fillFileInfo(fileInfo)) {
                    return false;
                }
                if (hdu.empty())  // use first when required
                    hdu = fileInfo->hdu_list(0);
                extFileInfoOK = infoLoader.fillFileExtInfo(extendedInfo, hdu, message);
            } catch (casacore::AipsError& ex) {
                message = ex.getMesg();
                extFileInfoOK = false;
            }
        } else {
            message = "File " + filename + " does not exist.";
            extFileInfoOK = false;
        }
    } catch (casacore::AipsError& err) {
        message = err.getMesg();
        extFileInfoOK = false;
    }
    return extFileInfoOK;
}

void Session::resetFileInfo(bool create) {
    // delete old file info pointers
    if (selectedFileInfo != nullptr) delete selectedFileInfo;
    if (selectedFileInfoExtended != nullptr) delete selectedFileInfoExtended;
    // optionally create new ones
    if (create) {
        selectedFileInfo = new CARTA::FileInfo();
        selectedFileInfoExtended = new CARTA::FileInfoExtended();
    } else {
        selectedFileInfo = nullptr;
        selectedFileInfoExtended = nullptr;
    }
}

// *********************************************************************************
// CARTA ICD implementation

void Session::onRegisterViewer(const CARTA::RegisterViewer& message, uint32_t requestId) {
    auto sessionId = message.session_id();
    bool success(false);
    std::string error;
    CARTA::SessionType type(CARTA::SessionType::NEW);
    // check session id
    if (sessionId.empty()) {
        sessionId = uuid;
        success = true;
    } else {
        type = CARTA::SessionType::RESUMED;
        if (sessionId.compare(uuid) != 0) {  // invalid session id
            error = "Cannot resume session id " + sessionId;
        } else {
            success = true;
        }
    }
    // response
    CARTA::RegisterViewerAck ackMessage;
    ackMessage.set_session_id(sessionId);
    ackMessage.set_success(success);
    ackMessage.set_message(error);
    ackMessage.set_session_type(type);
    sendEvent("REGISTER_VIEWER_ACK", requestId, ackMessage);
}

void Session::onFileListRequest(const CARTA::FileListRequest& request, uint32_t requestId) {
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

    CARTA::FileListResponse response;
    getFileList(response, folder);
    sendEvent("FILE_LIST_RESPONSE", requestId, response);
    filelistFolder = "nofolder";  // ready for next file list request
}

void Session::onFileInfoRequest(const CARTA::FileInfoRequest& request, uint32_t requestId) {
    CARTA::FileInfoResponse response;
    auto fileInfo = response.mutable_file_info();
    auto fileInfoExtended = response.mutable_file_info_extended();
    string message;
    bool success = fillExtendedFileInfo(fileInfoExtended, fileInfo, request.directory(), request.file(), request.hdu(), message);
    if (success) { // save a copy
        resetFileInfo(true);
        *selectedFileInfo = response.file_info();
        *selectedFileInfoExtended = response.file_info_extended();
    }
    response.set_success(success);
    response.set_message(message);
    sendEvent("FILE_INFO_RESPONSE", requestId, response);
}

void Session::onOpenFile(const CARTA::OpenFile& message, uint32_t requestId) {
    // Create Frame and send reponse message
    auto directory(message.directory());
    auto filename(message.file());
    auto hdu(message.hdu());
    auto fileId(message.file_id());
    // response message:
    CARTA::OpenFileAck ack;
    ack.set_file_id(fileId);
    string errMessage;
    bool infoLoaded((selectedFileInfo != nullptr) && 
        (selectedFileInfoExtended != nullptr) && 
        (selectedFileInfo->name() == filename)); // correct file loaded
    if (!infoLoaded) { // load from image file
        resetFileInfo(true);
        infoLoaded = fillExtendedFileInfo(selectedFileInfoExtended, selectedFileInfo, message.directory(),
            message.file(), hdu, errMessage);
    }
    bool success(false);
    if (!infoLoaded) {
        resetFileInfo(); // clean up
    } else {
        // Set hdu if empty
        if (hdu.empty())  // use first
            hdu = selectedFileInfo->hdu_list(0);
        // form path with filename
        casacore::Path rootPath(rootFolder);
        rootPath.append(directory);
        rootPath.append(filename);
        string absFilename(rootPath.resolvedName());
        // create Frame for open file
        auto frame = std::unique_ptr<Frame>(new Frame(uuid, absFilename, hdu));
        if (frame->isValid()) {
            std::unique_lock<std::mutex> lock(frameMutex); // open/close lock
            frames[fileId] = move(frame);
            lock.unlock();
            newFrame = true;
            // copy file info, extended file info
            CARTA::FileInfo* responseFileInfo = new CARTA::FileInfo();
            responseFileInfo->set_name(selectedFileInfo->name());
            responseFileInfo->set_type(selectedFileInfo->type());
            responseFileInfo->set_size(selectedFileInfo->size());
            responseFileInfo->add_hdu_list(hdu); // loaded hdu only
            *ack.mutable_file_info() = *responseFileInfo;
            *ack.mutable_file_info_extended() = *selectedFileInfoExtended;
            success = true;
        } else {
            errMessage = "Could not load image";
        }
    }
    ack.set_success(success);
    ack.set_message(errMessage);
    sendEvent("OPEN_FILE_ACK", requestId, ack);
}

void Session::onCloseFile(const CARTA::CloseFile& message, uint32_t requestId) {
    auto fileId = message.file_id();
    std::unique_lock<std::mutex> lock(frameMutex);
    if (fileId == -1) {
        for (auto& frame : frames) {
            frame.second.reset();  // delete Frame
        }
        frames.clear();
    } else if (frames.count(fileId)) {
        frames[fileId].reset();
        frames.erase(fileId);
    }
}

void Session::onSetImageView(const CARTA::SetImageView& message, uint32_t requestId) {
    auto fileId = message.file_id();
    if (frames.count(fileId)) {
        try {
            CARTA::ImageBounds bounds(message.image_bounds());
            int mip(message.mip());
            if (frames.at(fileId)->setBounds(bounds, mip)) {
                std::vector<float> imageData = frames.at(fileId)->getImageData(bounds, mip);
                if (!imageData.empty()) {
                    CompressionSettings csettings;
                    csettings.type = message.compression_type();
                    csettings.quality = message.compression_quality();
                    csettings.nsubsets = message.num_subsets();
                    frames.at(fileId)->setCompression(csettings);
                    // RESPONSE: raster/histogram data
                    CARTA::RasterImageData rasterImageData;
                    rasterImageData.set_stokes(frames.at(fileId)->currentStokes());
                    rasterImageData.set_channel(frames.at(fileId)->currentChannel());
                    if (newFrame) {
                        CARTA::RegionHistogramData* histogramData = getRegionHistogramData(fileId, IMAGE_REGION_ID);
                        rasterImageData.set_allocated_channel_histogram_data(histogramData);
                        newFrame = false;
                    }
                    sendRasterImageData(fileId, rasterImageData, imageData, bounds, mip, csettings);
                } else {
                    string error = "Raster image data failed to load";
                    sendLogEvent(error, {"raster"}, CARTA::ErrorSeverity::ERROR);
                }
            } else {
                sendLogEvent("Image view out of bounds", {"view"}, CARTA::ErrorSeverity::ERROR);
            }
        } catch (std::out_of_range& rangeError) {   // unordered_map.at() exception
            std::string error = fmt::format("File id {} closed", fileId);
            sendLogEvent(error, {"view"}, CARTA::ErrorSeverity::DEBUG);
        }
    } else {
        string error = fmt::format("File id {} not found", fileId);
        sendLogEvent(error, {"view"}, CARTA::ErrorSeverity::DEBUG);
    }
}

void Session::onSetImageChannels(const CARTA::SetImageChannels& message, uint32_t requestId) {
    auto fileId(message.file_id());
    if (frames.count(fileId)) {
        try {
            std::string errMessage;
            auto channel = message.channel();
            auto stokes = message.stokes();
            bool stokesChanged(stokes != frames.at(fileId)->currentStokes());
            if (frames.at(fileId)->setImageChannels(channel, stokes, errMessage)) {
                auto bounds = frames.at(fileId)->currentBounds();
                auto mip = frames.at(fileId)->currentMip();
                std::vector<float> imageData = frames.at(fileId)->getImageData(bounds, mip);
                if (!imageData.empty()) {
                    CompressionSettings csettings = frames.at(fileId)->compressionSettings();
                    CARTA::RegionHistogramData* histogramData = getRegionHistogramData(fileId, IMAGE_REGION_ID);
                    // RESPONSE: updated raster/histogram, spatial profile, spectral profile
                    CARTA::RasterImageData rasterImageData;
                    rasterImageData.set_stokes(stokes);
                    rasterImageData.set_channel(channel);
                    rasterImageData.set_allocated_channel_histogram_data(histogramData);
                    sendRasterImageData(fileId, rasterImageData, imageData, bounds, mip, csettings);
                    sendSpatialProfileData(fileId, CURSOR_REGION_ID);
                    if (stokesChanged)
                        sendSpectralProfileData(fileId, CURSOR_REGION_ID);
                } else {
                    string error = "Raster image data failed to load";
                    sendLogEvent(error, {"raster"}, CARTA::ErrorSeverity::ERROR);
                }
            } else {
                if (!errMessage.empty())
                    sendLogEvent(errMessage, {"channels"}, CARTA::ErrorSeverity::ERROR);
            }
        } catch (std::out_of_range& rangeError) {
            string error = fmt::format("File id {} closed", fileId);
            sendLogEvent(error, {"channels"}, CARTA::ErrorSeverity::DEBUG);
        }
    } else {
        string error = fmt::format("File id {} not found", fileId);
        sendLogEvent(error, {"channels"}, CARTA::ErrorSeverity::DEBUG);
    }
}

void Session::onSetCursor(const CARTA::SetCursor& message, uint32_t requestId) {
    auto fileId(message.file_id());
    if (frames.count(fileId)) {
        try {
            if (frames.at(fileId)->setCursorRegion(CURSOR_REGION_ID, message.point())) {
                // RESPONSE
                if (message.has_spatial_requirements()) {
                    onSetSpatialRequirements(message.spatial_requirements(), requestId);
                    sendSpectralProfileData(fileId, CURSOR_REGION_ID);
                } else {
                    sendSpatialProfileData(fileId, CURSOR_REGION_ID);
                    sendSpectralProfileData(fileId, CURSOR_REGION_ID);
                }
            } else {
                string error = "Cursor point out of range";
                sendLogEvent(error, {"cursor"}, CARTA::ErrorSeverity::ERROR);
            }
        } catch (std::out_of_range& rangeError) {
            string error = fmt::format("File id {} closed", fileId);
            sendLogEvent(error, {"cursor"}, CARTA::ErrorSeverity::DEBUG);
        }
    } else {
        string error = fmt::format("File id {} not found", fileId);
        sendLogEvent(error, {"cursor"}, CARTA::ErrorSeverity::DEBUG);
    }
}

void Session::onSetRegion(const CARTA::SetRegion& message, uint32_t requestId) {
    auto fileId(message.file_id());
    auto regionId(message.region_id());
    std::string errMessage;
    bool success(false);

    if (frames.count(fileId)) {
        try {
            if (message.region_id() <= 0) { // get region id unique across all frames
                for (auto& frame : frames) { // frames = map<fileId, unique_ptr<Frame>>
                    regionId = std::max(regionId, frame.second->getMaxRegionId());
                }
                ++regionId; // get next available
                if (regionId == 0) ++regionId; // reserved for cursor
            }
            std::vector<int> stokes = {message.stokes().begin(), message.stokes().end()};
            std::vector<CARTA::Point> points = {message.control_points().begin(), message.control_points().end()};
            auto& frame = frames[fileId];  // use frame in SetRegion message
            success = frames.at(fileId)->setRegion(regionId, message.region_name(), message.region_type(),
                message.channel_min(), message.channel_max(), stokes, points, message.rotation(), errMessage);
        } catch (std::out_of_range& rangeError) {
            errMessage = fmt::format("File id {} closed", fileId);
        }
    } else {
        errMessage = fmt::format("File id {} not found", fileId);
    }
    // RESPONSE
    CARTA::SetRegionAck ack;
    ack.set_region_id(regionId);
    ack.set_success(success);
    ack.set_message(errMessage);
    sendEvent("SET_REGION_ACK", requestId, ack);
}

void Session::onRemoveRegion(const CARTA::RemoveRegion& message, uint32_t requestId) {
    auto regionId(message.region_id());
    for (auto& frame : frames) {  // frames = map<fileId, unique_ptr<Frame>>
        if (frame.second) frame.second->removeRegion(regionId);
    }
}

void Session::onSetSpatialRequirements(const CARTA::SetSpatialRequirements& message, uint32_t requestId) {
    auto fileId(message.file_id());
    if (frames.count(fileId)) {
        try {
            auto regionId = message.region_id();
            if (frames.at(fileId)->setRegionSpatialRequirements(regionId,
                std::vector<std::string>(message.spatial_profiles().begin(), message.spatial_profiles().end()))) {
                // RESPONSE
                sendSpatialProfileData(fileId, regionId);
            } else {
                string error = fmt::format("Spatial requirements for region id {} failed to validate ", regionId);
                sendLogEvent(error, {"spatial"}, CARTA::ErrorSeverity::ERROR);
            }
        } catch (std::out_of_range& rangeError) {
            string error = fmt::format("File id {} closed", fileId);
            sendLogEvent(error, {"spatial"}, CARTA::ErrorSeverity::DEBUG);
        }
    } else {
        string error = fmt::format("File id {} not found", fileId);
        sendLogEvent(error, {"spatial"}, CARTA::ErrorSeverity::DEBUG);
    }
}

void Session::onSetHistogramRequirements(const CARTA::SetHistogramRequirements& message, uint32_t requestId) {
    auto fileId(message.file_id());
    if (frames.count(fileId)) {
        try {
            auto regionId = message.region_id();
            if (frames.at(fileId)->setRegionHistogramRequirements(regionId,
                std::vector<CARTA::SetHistogramRequirements_HistogramConfig>(message.histograms().begin(),
                message.histograms().end()))) {
                if (regionId == CUBE_REGION_ID) {
                    // RESPONSE
                    sendCubeHistogramData(message, requestId);
                } else {
                    CARTA::RegionHistogramData* histogramData = getRegionHistogramData(fileId, regionId);
                    if (histogramData != nullptr) {  // RESPONSE
                        sendFileEvent(fileId, "REGION_HISTOGRAM_DATA", 0, *histogramData);
                    } else {
                        string error = "Failed to load histogram data";
                        sendLogEvent(error, {"histogram"}, CARTA::ErrorSeverity::ERROR);
                    }
                }
            } else {
                string error = fmt::format("Histogram requirements for region id {} failed to validate ", regionId);
                sendLogEvent(error, {"histogram"}, CARTA::ErrorSeverity::ERROR);
            }
        } catch (std::out_of_range& rangeError) {
            string error = fmt::format("File id {} closed", fileId);
            sendLogEvent(error, {"histogram"}, CARTA::ErrorSeverity::DEBUG);
        }
    } else {
        string error = fmt::format("File id {} not found", fileId);
        sendLogEvent(error, {"histogram"}, CARTA::ErrorSeverity::DEBUG);
    }
}

void Session::onSetSpectralRequirements(const CARTA::SetSpectralRequirements& message, uint32_t requestId) {
    auto fileId(message.file_id());
    if (frames.count(fileId)) {
        try {
            auto regionId = message.region_id();
            if (frames.at(fileId)->setRegionSpectralRequirements(regionId,
                std::vector<CARTA::SetSpectralRequirements_SpectralConfig>(message.spectral_profiles().begin(),
                message.spectral_profiles().end()))) {
                // RESPONSE
                sendSpectralProfileData(fileId, regionId);
            } else {
                string error = fmt::format("Spectral requirements for region id {} failed to validate ", regionId);
                sendLogEvent(error, {"spectral"}, CARTA::ErrorSeverity::ERROR);
            }
        } catch (std::out_of_range& rangeError) {
            string error = fmt::format("File id {} closed", fileId);
            sendLogEvent(error, {"spectral"}, CARTA::ErrorSeverity::DEBUG);
        }
    } else {
        string error = fmt::format("File id {} not found", fileId);
        sendLogEvent(error, {"spectral"}, CARTA::ErrorSeverity::DEBUG);
    }
}

void Session::onSetStatsRequirements(const CARTA::SetStatsRequirements& message, uint32_t requestId) {
    auto fileId(message.file_id());
    if (frames.count(fileId)) {
        try {
            auto regionId = message.region_id();
            if (frames.at(fileId)->setRegionStatsRequirements(regionId, std::vector<int>(message.stats().begin(),
                message.stats().end()))) {
                // RESPONSE
                sendRegionStatsData(fileId, regionId);
            } else {
                string error = fmt::format("Stats requirements for region id {} failed to validate ", regionId);
                sendLogEvent(error, {"stats"}, CARTA::ErrorSeverity::ERROR);
            }
        } catch (std::out_of_range& rangeError) {
            string error = fmt::format("File id {} closed", fileId);
            sendLogEvent(error, {"stats"}, CARTA::ErrorSeverity::DEBUG);
        }
    } else {
        string error = fmt::format("File id {} not found", fileId);
        sendLogEvent(error, {"stats"}, CARTA::ErrorSeverity::DEBUG);
    }
}

// ******** SEND DATA STREAMS *********

// Histogram message; sent separately or within RasterImageData
CARTA::RegionHistogramData* Session::getRegionHistogramData(const int32_t fileId, const int32_t regionId) {
    CARTA::RegionHistogramData* histogramMessage(nullptr);
    if (frames.count(fileId)) {
        try {
            histogramMessage = new CARTA::RegionHistogramData();
            if (frames.at(fileId)->fillRegionHistogramData(regionId, histogramMessage)) {
                histogramMessage->set_file_id(fileId);
                histogramMessage->set_region_id(regionId);
            } else {
                delete histogramMessage;
                histogramMessage = nullptr;
            }
        } catch (std::out_of_range& rangeError) {
            string error = fmt::format("File id {} closed", fileId);
            sendLogEvent(error, {"histogram"}, CARTA::ErrorSeverity::DEBUG);
        }
    } else {
        string error = fmt::format("File id {} not found", fileId);
        sendLogEvent(error, {"histogram"}, CARTA::ErrorSeverity::DEBUG);
    }
    return histogramMessage;
}

void Session::sendCubeHistogramData(const CARTA::SetHistogramRequirements& message,
        uint32_t requestId) {
    auto fileId = message.file_id();
    if (frames.count(fileId)) {
        try {
            if (message.histograms_size() == 0) { // cancel!
                histogramProgress.fetch_and_store(HISTOGRAM_CANCEL);
                sendLogEvent("Histogram cancelled", {"histogram"}, CARTA::ErrorSeverity::INFO);
                return;
            } else {
                auto regionId = message.region_id(); // CUBE_REGION_ID
                auto channel = message.histograms(0).channel();
                auto numbins = message.histograms(0).num_bins();
                int stokes(frames.at(fileId)->currentStokes());
                CARTA::RegionHistogramData histogramMessage;
                createCubeHistogramMessage(histogramMessage, fileId, stokes, 1.0);
                CARTA::Histogram* histogram = histogramMessage.add_histograms();
                if (frames.at(fileId)->getRegionHistogram(regionId, channel, stokes, numbins, *histogram)) {
                    // use stored cube histogram
                    sendFileEvent(fileId, "REGION_HISTOGRAM_DATA", requestId, histogramMessage);
                } else if (frames.at(fileId)->nchannels() == 1) { 
                    // use per-channel histogram for channel 0
                    int channum(0);
                    if (frames.at(fileId)->getRegionHistogram(IMAGE_REGION_ID, channum, stokes, numbins,
                            *histogram)) { // use stored channel 0 histogram
                        sendFileEvent(fileId, "REGION_HISTOGRAM_DATA", requestId, histogramMessage);
                    } else { // calculate channel 0 histogram
                        float minval, maxval;
                        if (!frames.at(fileId)->getRegionMinMax(IMAGE_REGION_ID, channum, stokes, minval, maxval))
                            frames.at(fileId)->calcRegionMinMax(IMAGE_REGION_ID, channum, stokes, minval, maxval);
                        frames.at(fileId)->calcRegionHistogram(IMAGE_REGION_ID, channum, stokes, numbins, minval,
                            maxval, *histogram);
                        // send completed histogram
                        sendFileEvent(fileId, "REGION_HISTOGRAM_DATA", requestId, histogramMessage);
                    }
                } else { // calculate cube histogram
                    histogramProgress.fetch_and_store(0.0);  // start at 0
                    auto tStart = std::chrono::high_resolution_clock::now();
                    // determine cube min and max values
                    float cubemin(FLT_MAX), cubemax(FLT_MIN);
                    size_t nchan(frames.at(fileId)->nchannels());
                    for (size_t chan=0; chan < nchan; ++chan) {
                        // minmax for this channel
                        float chanmin, chanmax;
                        if (!frames.at(fileId)->getRegionMinMax(IMAGE_REGION_ID, chan, stokes, chanmin, chanmax))
                            frames.at(fileId)->calcRegionMinMax(IMAGE_REGION_ID, chan, stokes, chanmin, chanmax);
                        cubemin = std::min(cubemin, chanmin);
                        cubemax = std::max(cubemax, chanmax);

                        // check for cancel
                        if (histogramProgress == HISTOGRAM_CANCEL)
                            break;

                        // check for progress update
                        auto tEnd = std::chrono::high_resolution_clock::now();
                        auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart).count();
                        if ((dt/1e3) > 2.0) {  // send progress
                            float thischan(chan), allchans(nchan * 2); // go through chans twice
                            float progress = thischan / allchans;
                            CARTA::RegionHistogramData histogramProgressMsg;
                            createCubeHistogramMessage(histogramProgressMsg, fileId, stokes, progress);
                            CARTA::Histogram* histogram = histogramMessage.add_histograms();  // blank
                            sendFileEvent(fileId, "REGION_HISTOGRAM_DATA", requestId, histogramProgressMsg);
                            tStart = tEnd;
                        }
                    }
                    // save min,max in cube region
                    if (histogramProgress > HISTOGRAM_CANCEL)
                        frames.at(fileId)->setRegionMinMax(regionId, channel, stokes, cubemin, cubemax);

                    // check cancel and proceed
                    if (histogramProgress > HISTOGRAM_CANCEL) {
                        // send progress message: half done
                        float progress = 0.50;
                        histogramMessage.set_progress(progress);
                        sendFileEvent(fileId, "REGION_HISTOGRAM_DATA", requestId, histogramMessage);

                        // get histogram bins for each channel and accumulate bin counts
                        std::vector<int> cubeBins;
                        CARTA::Histogram chanHistogram;  // histogram for each channel
                        for (size_t chan=0; chan < nchan; ++chan) { 
                            frames.at(fileId)->calcRegionHistogram(regionId, chan, stokes, numbins, cubemin,
                                cubemax, chanHistogram);
                            // add channel bins to cube bins
                            if (chan==0) {
                                cubeBins = {chanHistogram.bins().begin(), chanHistogram.bins().end()};
                            } else { // add chan histogram bins to cube histogram bins
                                std::transform(chanHistogram.bins().begin(), chanHistogram.bins().end(), cubeBins.begin(),
                                    cubeBins.begin(), std::plus<int>());
                            }

                            // check for cancel
                            if (histogramProgress == HISTOGRAM_CANCEL)
                                break;

                            // check for progress update
                            auto tEnd = std::chrono::high_resolution_clock::now();
                            auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart).count();
                            if ((dt/1e3) > 2.0) { // send progress
                                float thischan(chan), allchans(nchan * 2); // go through chans twice
                                progress = 0.5 + (thischan / allchans);
                                CARTA::RegionHistogramData histogramProgressMsg;
                                createCubeHistogramMessage(histogramProgressMsg, fileId, stokes, progress);
                                auto cubeHistogram = histogramProgressMsg.add_histograms();
                                cubeHistogram->set_channel(ALL_CHANNELS);
                                cubeHistogram->set_num_bins(chanHistogram.num_bins());
                                cubeHistogram->set_bin_width(chanHistogram.bin_width());
                                cubeHistogram->set_first_bin_center(chanHistogram.first_bin_center());
                                *cubeHistogram->mutable_bins() = {cubeBins.begin(), cubeBins.end()};
                                sendFileEvent(fileId, "REGION_HISTOGRAM_DATA", requestId, histogramProgressMsg);
                                tStart = tEnd;
                            }
                        }
                        if (histogramProgress > HISTOGRAM_CANCEL) {
                            // send completed cube histogram
                            progress = HISTOGRAM_COMPLETE;
                            CARTA::RegionHistogramData finalHistogramMessage;
                            createCubeHistogramMessage(finalHistogramMessage, fileId, stokes, progress);
                            auto cubeHistogram = finalHistogramMessage.add_histograms();
                            // fill histogram fields from last channel histogram
                            cubeHistogram->set_channel(ALL_CHANNELS);
                            cubeHistogram->set_num_bins(chanHistogram.num_bins());
                            cubeHistogram->set_bin_width(chanHistogram.bin_width());
                            cubeHistogram->set_first_bin_center(chanHistogram.first_bin_center());
                            *cubeHistogram->mutable_bins() = {cubeBins.begin(), cubeBins.end()};

                            // save cube histogram
                            frames.at(fileId)->setRegionHistogram(regionId, channel, stokes, *cubeHistogram);
                            // send completed histogram message
                            sendFileEvent(fileId, "REGION_HISTOGRAM_DATA", requestId, finalHistogramMessage);
                        }
                    }
                }
            }
        } catch (std::out_of_range& rangeError) {
            string error = fmt::format("File id {} closed", fileId);
            sendLogEvent(error, {"histogram"}, CARTA::ErrorSeverity::DEBUG);
        }
    } else {
        string error = fmt::format("File id {} not found", fileId);
        sendLogEvent(error, {"histogram"}, CARTA::ErrorSeverity::DEBUG);
    }
}

void Session::createCubeHistogramMessage(CARTA::RegionHistogramData& message, int fileId, int stokes, float progress) {
    // check for cancel then update progress and make new message
    if (histogramProgress != HISTOGRAM_CANCEL) {
        histogramProgress.fetch_and_store(progress);
        message.set_file_id(fileId);
        message.set_region_id(CUBE_REGION_ID);
        message.set_stokes(stokes);
        message.set_progress(progress);
    }
}

void Session::sendRasterImageData(int fileId, CARTA::RasterImageData& rasterImageData,
    std::vector<float>& imageData, CARTA::ImageBounds& bounds, int mip, CompressionSettings& compression) {
    if (frames.count(fileId)) {
        try {
            if (!imageData.empty()) {
                rasterImageData.set_file_id(fileId);
                rasterImageData.mutable_image_bounds()->set_x_min(bounds.x_min());
                rasterImageData.mutable_image_bounds()->set_x_max(bounds.x_max());
                rasterImageData.mutable_image_bounds()->set_y_min(bounds.y_min());
                rasterImageData.mutable_image_bounds()->set_y_max(bounds.y_max());
                rasterImageData.set_mip(mip);
                auto compressionType = compression.type;
                if (compressionType == CARTA::CompressionType::NONE) {
                    rasterImageData.set_compression_type(CARTA::CompressionType::NONE);
                    rasterImageData.set_compression_quality(0);
                    rasterImageData.add_image_data(imageData.data(), imageData.size() * sizeof(float));
                    // Send completed event to client
                    sendFileEvent(fileId, "RASTER_IMAGE_DATA", 0, rasterImageData);
                } else if (compressionType == CARTA::CompressionType::ZFP) {
                    int precision = lround(compression.quality);
                    rasterImageData.set_compression_type(CARTA::CompressionType::ZFP);
                    rasterImageData.set_compression_quality(precision);

                    auto rowLength = (bounds.x_max() - bounds.x_min()) / mip;
                    auto numRows = (bounds.y_max() - bounds.y_min()) / mip;
                    auto numSubsets = compression.nsubsets;

                    std::vector<std::vector<char>> compressionBuffers(numSubsets);
                    std::vector<size_t> compressedSizes(numSubsets);
                    std::vector<std::vector<int32_t>> nanEncodings(numSubsets);

                    auto N = std::min(numSubsets, MAX_SUBSETS);
                    auto range = tbb::blocked_range<int>(0, N);
                    auto loop = [&](const tbb::blocked_range<int> &r) {
                        for(int i = r.begin(); i != r.end(); ++i) {
                            int subsetRowStart = i * (numRows / N);
                            int subsetRowEnd = (i + 1) * (numRows / N);
                            if (i == N - 1) {
                                subsetRowEnd = numRows;
                            }
                            int subsetElementStart = subsetRowStart * rowLength;
                            int subsetElementEnd = subsetRowEnd * rowLength;
                            nanEncodings[i] = getNanEncodingsBlock(imageData, subsetElementStart, rowLength,
                                subsetRowEnd - subsetRowStart);
                            compress(imageData, subsetElementStart, compressionBuffers[i], compressedSizes[i],
                                rowLength, subsetRowEnd - subsetRowStart, precision);
                        }
                    };
                    auto tStartCompress = std::chrono::high_resolution_clock::now();
                    tbb::parallel_for(range, loop);
                    auto tEndCompress = std::chrono::high_resolution_clock::now();
                    auto dtCompress = std::chrono::duration_cast<std::chrono::microseconds>(tEndCompress - tStartCompress).count();

                    // Complete message
                    for (auto i = 0; i < numSubsets; i++) {
                        rasterImageData.add_image_data(compressionBuffers[i].data(), compressedSizes[i]);
                        rasterImageData.add_nan_encodings((char*) nanEncodings[i].data(),
                            nanEncodings[i].size() * sizeof(int));
                    }

                    if (verboseLogging) {
                        string compressionInfo = fmt::format(
                            "Image data of size {:.1f} kB compressed to {:.1f} kB in {} ms at {:.2f} MPix/s",
                            numRows * rowLength * sizeof(float) / 1e3,
                            std::accumulate(compressedSizes.begin(), compressedSizes.end(), 0) * 1e-3,
                            1e-3 * dtCompress,
                            (float) (numRows * rowLength) / dtCompress);
                        sendLogEvent(compressionInfo, {"zfp"}, CARTA::ErrorSeverity::DEBUG);
                    }
                    // Send completed event to client
                    sendFileEvent(fileId, "RASTER_IMAGE_DATA", 0, rasterImageData);
                } else {
                    string error = "SZ compression not implemented";
                    sendLogEvent(error, {"raster"}, CARTA::ErrorSeverity::ERROR);
                }
            } else {
                string error = "Raster image data failed to load";
                sendLogEvent(error, {"raster"}, CARTA::ErrorSeverity::ERROR);
            }
        } catch (std::out_of_range& rangeError) {
            string error = fmt::format("File id {} closed", fileId);
            sendLogEvent(error, {"raster"}, CARTA::ErrorSeverity::DEBUG);
        }
    } else {
        string error = fmt::format("File id {} not found", fileId);
        sendLogEvent(error, {"raster"}, CARTA::ErrorSeverity::DEBUG);
    }
}

void Session::sendSpatialProfileData(int fileId, int regionId) {
    if (frames.count(fileId)) {
        try {
            if (regionId == CURSOR_REGION_ID && !frames.at(fileId)->isCursorSet()) {
                return;  // do not send profile unless frontend set cursor
            }
            CARTA::SpatialProfileData spatialProfileData;
            if (frames.at(fileId)->fillSpatialProfileData(regionId, spatialProfileData)) {
                spatialProfileData.set_file_id(fileId);
                spatialProfileData.set_region_id(regionId);
                sendFileEvent(fileId, "SPATIAL_PROFILE_DATA", 0, spatialProfileData);
            }
        } catch (std::out_of_range& rangeError) {
            string error = fmt::format("File id {} closed", fileId);
            sendLogEvent(error, {"spatial"}, CARTA::ErrorSeverity::DEBUG);
        }
    } else {
        string error = fmt::format("File id {} not found", fileId);
        sendLogEvent(error, {"spatial"}, CARTA::ErrorSeverity::DEBUG);
    }
}

void Session::sendSpectralProfileData(int fileId, int regionId) {
    if (frames.count(fileId)) {
        try {
            if (regionId == CURSOR_REGION_ID && !frames.at(fileId)->isCursorSet()) {
                return;  // do not send profile unless frontend set cursor
            }
            CARTA::SpectralProfileData spectralProfileData;
            if (frames.at(fileId)->fillSpectralProfileData(regionId, spectralProfileData)) {
                spectralProfileData.set_file_id(fileId);
                spectralProfileData.set_region_id(regionId);
                sendFileEvent(fileId, "SPECTRAL_PROFILE_DATA", 0, spectralProfileData);
            }
        } catch (std::out_of_range& rangeError) {
            string error = fmt::format("File id {} closed", fileId);
            sendLogEvent(error, {"spectral"}, CARTA::ErrorSeverity::DEBUG);
        }
    } else {
        string error = fmt::format("File id {} not found", fileId);
        sendLogEvent(error, {"spectral"}, CARTA::ErrorSeverity::DEBUG);
    }
}

void Session::sendRegionStatsData(int fileId, int regionId) {
    if (frames.count(fileId)) {
        try {
            CARTA::RegionStatsData regionStatsData;
            if (frames.at(fileId)->fillRegionStatsData(regionId, regionStatsData)) {
                regionStatsData.set_file_id(fileId);
                regionStatsData.set_region_id(regionId);
                sendFileEvent(fileId, "REGION_STATS_DATA", 0, regionStatsData);
            }
        } catch (std::out_of_range& rangeError) {
            string error = fmt::format("File id {} closed", fileId);
            sendLogEvent(error, {"stats"}, CARTA::ErrorSeverity::DEBUG);
        }
    } else {
        string error = fmt::format("File id {} not found", fileId);
        sendLogEvent(error, {"stats"}, CARTA::ErrorSeverity::DEBUG);
    }
}

// *********************************************************************************
// SEND uWEBSOCKET MESSAGES

// Sends an event to the client with a given event name (padded/concatenated to 32 characters) and a given ProtoBuf message
void Session::sendEvent(string eventName, u_int64_t eventId, google::protobuf::MessageLite& message) {
    static const size_t eventNameLength = 32;
    int messageLength = message.ByteSize();
    size_t requiredSize = eventNameLength + 8 + messageLength;
    std::vector<char> msg(requiredSize, 0);
    std::copy_n(eventName.begin(), std::min(eventName.length(), eventNameLength), msg.begin());
    memcpy(msg.data() + eventNameLength, &eventId, 4);
    message.SerializeToArray(msg.data() + eventNameLength + 8, messageLength);
    out_msgs.push(msg);
    outgoing->send();
    //socket->send(msg.data(), msg.size(), uWS::BINARY);
}

void Session::sendFileEvent(int32_t fileId, string eventName, u_int64_t eventId,
    google::protobuf::MessageLite& message) {
    // do not send if file is closed
    if (frames.count(fileId))
        sendEvent(eventName, eventId, message);
}

void Session::sendPendingMessages() {
    // Do not parallelize: this must be done serially
    // due to the constraints of uWS.
    std::vector<char> msg;
    while(out_msgs.try_pop(msg)) {
        socket->send(msg.data(), msg.size(), uWS::BINARY);
    }
}

void Session::sendLogEvent(std::string message, std::vector<std::string> tags, CARTA::ErrorSeverity severity) {
    CARTA::ErrorData errorData;
    errorData.set_message(message);
    errorData.set_severity(severity);
    *errorData.mutable_tags() = {tags.begin(), tags.end()};
    sendEvent("ERROR_DATA", 0, errorData);
    if ((severity > CARTA::ErrorSeverity::DEBUG) || verboseLogging)
        log(uuid, message);
}
