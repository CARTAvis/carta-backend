#include "Session.h"
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

// Default constructor. Associates a websocket with a UUID and sets the base folder for all files
Session::Session(uWS::WebSocket<uWS::SERVER>* ws, std::string uuid, std::unordered_map<string, 
    std::vector<std::string>>& permissionsMap, bool enforcePermissions, std::string folder,
    uS::Async *outgoing, bool verbose)
    : uuid(std::move(uuid)),
      socket(ws),
      permissionsMap(permissionsMap),
      permissionsEnabled(enforcePermissions),
      baseFolder(folder),
      filelistFolder("nofolder"),
      verboseLogging(verbose),
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

CARTA::FileListResponse Session::getFileList(string folder) {
    // fill FileListResponse
    casacore::Path fullPath(baseFolder);
    CARTA::FileListResponse fileList;
    if (folder.length() && folder != "/") {
        fullPath.append(folder);
        fileList.set_directory(folder);
        fileList.set_parent(fullPath.dirName());
    }

    casacore::File folderPath(fullPath);
    string message;

    try {
        if (checkPermissionForDirectory(folder) && folderPath.exists() && folderPath.isDirectory()) {
            casacore::Directory startDir(fullPath);
            casacore::DirectoryIterator dirIter(startDir);
            while (!dirIter.pastEnd()) {
                casacore::File ccfile(dirIter.file());  // directory is also a File
                if (ccfile.exists() && ccfile.path().baseName().firstchar() != '.') {  // ignore hidden files/folders
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
				log(uuid, imageTypeMsg);
                            }
                        } else if (ccfile.isRegular(true) && ccfile.isReadable()) {
                            casacore::ImageOpener::ImageTypes imType = casacore::ImageOpener::imageType(fullpath);
                            if ((imType==casacore::ImageOpener::FITS) || (imType==casacore::ImageOpener::HDF5))
                                addImage = true;
                        }

                        if (addImage) { // add image to file list
                            auto fileInfo = fileList.add_files();
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
            return fileList;
        }
    } catch (casacore::AipsError& err) {
        log(uuid, "Error: {}", err.getMesg().c_str());
        sendLogEvent(err.getMesg(), {"file-list"}, CARTA::ErrorSeverity::ERROR);
        fileList.set_success(false);
        fileList.set_message(err.getMesg());
        return fileList;
    }
    fileList.set_success(true);
    return fileList;
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
    casacore::Path ccpath(baseFolder);
    ccpath.append(folder);
    ccpath.append(filename);
    casacore::File ccfile(ccpath);
    if (ccfile.exists()) {
        casacore::String fullname(ccfile.path().absoluteName());
        try {
            FileInfoLoader infoLoader(fullname);
            if (!infoLoader.fillFileInfo(fileInfo)) {
                return false;
            }
            extFileInfoOK = infoLoader.fillFileExtInfo(extendedInfo, hdu, message);
        } catch (casacore::AipsError& ex) {
            message = ex.getMesg();
            extFileInfoOK = false;
        }
    } else {
        message = "File " + filename + " does not exist.";
        extFileInfoOK = false;
    }

    return extFileInfoOK;
}


// *********************************************************************************
// CARTA ICD implementation

void Session::onRegisterViewer(const CARTA::RegisterViewer& message, uint32_t requestId) {
    apiKey = message.api_key();
    CARTA::RegisterViewerAck ackMessage;
    ackMessage.set_success(true);
    ackMessage.set_session_id(uuid);
    sendEvent("REGISTER_VIEWER_ACK", requestId, ackMessage);
}

void Session::onFileListRequest(const CARTA::FileListRequest& request, uint32_t requestId) {
    // initial folder is "" (use base directory)
    string folder = request.directory();
    if (folder == filelistFolder) // do not process same directory simultaneously
        return;
    else
        filelistFolder = folder;

    // strip baseFolder from folder
    string basePath(baseFolder);
    if (basePath.back()=='/') basePath.pop_back();
    if (folder.find(basePath)==0) {
        folder.replace(0, basePath.length(), "");
        if (folder.front()=='/') folder.replace(0,1,""); // remove leading '/'
    }
    CARTA::FileListResponse response = getFileList(folder);
    sendEvent("FILE_LIST_RESPONSE", requestId, response);
    filelistFolder = "nofolder";  // ready for next file list request
}

void Session::onFileInfoRequest(const CARTA::FileInfoRequest& request, uint32_t requestId) {
    CARTA::FileInfoResponse response;
    auto fileInfo = response.mutable_file_info();
    auto fileInfoExtended = response.mutable_file_info_extended();
    string message;
    bool success = fillExtendedFileInfo(fileInfoExtended, fileInfo, request.directory(), request.file(), request.hdu(), message);
    response.set_success(success);
    response.set_message(message);
    sendEvent("FILE_INFO_RESPONSE", requestId, response);
}

void Session::onOpenFile(const CARTA::OpenFile& message, uint32_t requestId) {
    auto fileId(message.file_id());
    CARTA::OpenFileAck ack;
    ack.set_file_id(fileId);
    auto fileInfo = ack.mutable_file_info();
    auto fileInfoExtended = ack.mutable_file_info_extended();
    string errMessage;
    bool success(false);
    if (fillExtendedFileInfo(fileInfoExtended, fileInfo, message.directory(), message.file(),
        message.hdu(), errMessage)) {
        // form filename with path
        casacore::Path path(baseFolder);
        path.append(message.directory());
        path.append(message.file());
        string filename(path.absoluteName());
        // create Frame for open file
        string hdu = fileInfo->hdu_list(0);
        auto frame = std::unique_ptr<Frame>(new Frame(uuid, filename, hdu));
        if (frame->isValid()) {
            success = true;
            std::unique_lock<std::mutex> lock(frameMutex);
            frames[fileId] = move(frame);
            lock.unlock();
            newFrame = true;
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
            string errMessage;
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
                    if (histogramData != nullptr) {
                        // RESPONSE
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
            histogramMessage->set_file_id(fileId);
            histogramMessage->set_region_id(regionId);
            frames.at(fileId)->fillRegionHistogramData(regionId, histogramMessage);
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
                int numbins(message.histograms(0).num_bins());
                numbins = (numbins == AUTO_BIN_SIZE ? frames.at(fileId)->calcAutoNumBins() : numbins);
                int stokes(frames.at(fileId)->currentStokes());
                size_t nchan(frames.at(fileId)->nchannels());
                bool histogramSent(false);
                if (nchan == 1) {  // use per-channel histogram for channel 0
                    int channum(0);
                    CARTA::RegionHistogramData histogramMessage;
                    createCubeHistogramMessage(histogramMessage, fileId, stokes, 1.0);
                    if (frames.at(fileId)->getChannelHistogramData(histogramMessage, channum, stokes, numbins)) {
                        sendFileEvent(fileId, "REGION_HISTOGRAM_DATA", requestId, histogramMessage);
                        histogramSent = true;
                    }
                }
                if (!histogramSent) {  // compute cube histogram
                    histogramProgress.fetch_and_store(0.0);  // start at 0
                    auto tStart = std::chrono::high_resolution_clock::now();
                    // determine min and max values
                    float minval(FLT_MAX), maxval(FLT_MIN);
                    for (size_t chan=0; chan < nchan; ++chan) {
                        float chanmin, chanmax;
                        std::vector<float> data = frames.at(fileId)->getImageChanData(chan);
                        frames.at(fileId)->getMinMax(chanmin, chanmax, data);
                        minval = std::min(minval, chanmin);
                        maxval = std::max(maxval, chanmax);

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
                            sendFileEvent(fileId, "REGION_HISTOGRAM_DATA", requestId, histogramProgressMsg);
                            tStart = tEnd;
                        }
                    }
                    if (histogramProgress > HISTOGRAM_CANCEL) {
                        // send message in case min/max took < 2s; progress is half done
                        float progress = 0.50;
                        CARTA::RegionHistogramData halfProgressMsg;
                        createCubeHistogramMessage(halfProgressMsg, fileId, stokes, progress);
                        sendFileEvent(fileId, "REGION_HISTOGRAM_DATA", requestId, halfProgressMsg);

                        // get histogram bins for each channel
                        std::valarray<int> cubeBins(0, numbins);  // accumulate bin counts
                        // create message for each channel histogram (not sent)
                        CARTA::RegionHistogramData chanHistogramMessage;
                        createCubeHistogramMessage(chanHistogramMessage, fileId, stokes, progress);
                        for (size_t chan=0; chan < nchan; ++chan) {
                            // get CARTA::Histogram for this channel
                            chanHistogramMessage.clear_histograms();
                            std::vector<float> data = frames.at(fileId)->getImageChanData(chan);
                            frames.at(fileId)->fillChannelHistogramData(chanHistogramMessage, data, chan, numbins, minval, maxval);
                            auto chanHistogram = chanHistogramMessage.histograms(0);

                            // add channel bins to cube bins
                            std::vector<int> channelBins = {chanHistogram.bins().begin(), chanHistogram.bins().end()};
                            std::valarray<int> channelVals(channelBins.data(), channelBins.size());
                            cubeBins += channelVals;

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
                                auto cubeHistogram = histogramProgressMsg.mutable_histograms(0);
                                cubeHistogram->set_channel(ALL_CHANNELS);
                                cubeHistogram->set_num_bins(chanHistogram.num_bins());
                                cubeHistogram->set_bin_width(chanHistogram.bin_width());
                                cubeHistogram->set_first_bin_center(chanHistogram.first_bin_center());
                                *cubeHistogram->mutable_bins() = {std::begin(cubeBins), std::end(cubeBins)};
                                sendFileEvent(fileId, "REGION_HISTOGRAM_DATA", requestId, histogramProgressMsg);
                                tStart = tEnd;
                            }
                        }
                        if (histogramProgress > HISTOGRAM_CANCEL) {
                            // send completed cube histogram
                            progress = HISTOGRAM_COMPLETE;
                            CARTA::RegionHistogramData finalHistogramMessage;
                            createCubeHistogramMessage(finalHistogramMessage, fileId, stokes, progress);
                            auto cubeHistogram = finalHistogramMessage.mutable_histograms(0);
                            // fill histogram fields from channel histogram
                            auto lastChanHistogram = chanHistogramMessage.histograms(0);
                            cubeHistogram->set_channel(ALL_CHANNELS);
                            cubeHistogram->set_num_bins(lastChanHistogram.num_bins());
                            cubeHistogram->set_bin_width(lastChanHistogram.bin_width());
                            cubeHistogram->set_first_bin_center(lastChanHistogram.first_bin_center());
                            // set cube histogram bins
                            *cubeHistogram->mutable_bins() = {std::begin(cubeBins), std::end(cubeBins)};
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
        auto newHistogram = message.add_histograms();
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
                        log(uuid, compressionInfo);
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
            } else {
                string error = "Spatial profile data failed to load";
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
            } else {
                string error = "Spectral profile data failed to load";
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

void Session::sendRegionStatsData(int fileId, int regionId) {
    if (frames.count(fileId)) {
        try {
            CARTA::RegionStatsData regionStatsData;
            if (frames.at(fileId)->fillRegionStatsData(regionId, regionStatsData)) {
                regionStatsData.set_file_id(fileId);
                regionStatsData.set_region_id(regionId);
                sendFileEvent(fileId, "REGION_STATS_DATA", 0, regionStatsData);
            } else {
                string error = "Region stats data failed to load";
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
}
