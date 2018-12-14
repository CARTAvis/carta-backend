#include "Session.h"
#include "FileInfoLoader.h"
#include "compression.h"
#include "util.h"
#include <carta-protobuf/error.pb.h>

#include <casacore/casa/OS/Path.h>
#include <casacore/casa/OS/DirectoryIterator.h>
#include <tbb/tbb.h>

using namespace std;
using namespace CARTA;

// Default constructor. Associates a websocket with a UUID and sets the base folder for all files
Session::Session(uWS::WebSocket<uWS::SERVER>* ws, std::string uuid, unordered_map<string, vector<string>>& permissionsMap, bool enforcePermissions, string folder, uS::Async *outgoing, bool verbose)
    : uuid(std::move(uuid)),
      socket(ws),
      permissionsMap(permissionsMap),
      permissionsEnabled(enforcePermissions),
      baseFolder(folder),
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

FileListResponse Session::getFileList(string folder) {
    // fill FileListResponse
    casacore::Path fullPath(baseFolder);
    FileListResponse fileList;
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
                if (ccfile.path().baseName().firstchar() != '.') {  // ignore hidden files/folders
                    casacore::String fullpath(ccfile.path().absoluteName());
                    try {
                        casacore::ImageOpener::ImageTypes imType = casacore::ImageOpener::imageType(fullpath);
                        bool addImage(false);
                        if (ccfile.isDirectory(true)) {
                            if ((imType==casacore::ImageOpener::AIPSPP) || (imType==casacore::ImageOpener::MIRIAD))
                                addImage = true;
                            else if (imType==casacore::ImageOpener::UNKNOWN) {
                                // Check if it is a directory and the user has permission to access it
                                casacore::String dirname(ccfile.path().baseName());
                                string pathNameRelative = (folder.length() && folder != "/") ? folder.append("/" + dirname) : dirname;
                                if (checkPermissionForDirectory(pathNameRelative))
                                   fileList.add_subdirectories(dirname);
                            }
                        } else if (ccfile.isRegular(true) &&
                            ((imType==casacore::ImageOpener::FITS) || (imType==casacore::ImageOpener::HDF5))) {
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

bool Session::fillFileInfo(FileInfo* fileInfo, const string& filename) {
    // fill FileInfo submessage
    FileInfoLoader infoLoader(filename);
    return infoLoader.fillFileInfo(fileInfo);
}

bool Session::fillExtendedFileInfo(FileInfoExtended* extendedInfo, FileInfo* fileInfo, 
        const string folder, const string filename, string hdu, string& message) {
    // fill FileInfoResponse submessages FileInfo and FileInfoExtended
    bool extFileInfoOK(true);
    casacore::Path ccpath(baseFolder);
    ccpath.append(folder);
    ccpath.append(filename);
    casacore::File ccfile(ccpath);
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
    return extFileInfoOK;
}


// *********************************************************************************
// CARTA ICD implementation

void Session::onRegisterViewer(const RegisterViewer& message, uint32_t requestId) {
    apiKey = message.api_key();
    RegisterViewerAck ackMessage;
    ackMessage.set_success(true);
    ackMessage.set_session_id(uuid);
    sendEvent("REGISTER_VIEWER_ACK", requestId, ackMessage);
}

void Session::onFileListRequest(const FileListRequest& request, uint32_t requestId) {
    string folder = request.directory();
    // strip baseFolder from folder
    string basePath(baseFolder);
    if (basePath.back()=='/') basePath.pop_back();
    if (folder.find(basePath)==0) {
        folder.replace(0, basePath.length(), "");
        if (folder.front()=='/') folder.replace(0,1,""); // remove leading '/'
    }
    FileListResponse response = getFileList(folder);
    sendEvent("FILE_LIST_RESPONSE", requestId, response);
}

void Session::onFileInfoRequest(const FileInfoRequest& request, uint32_t requestId) {
    FileInfoResponse response;
    auto fileInfo = response.mutable_file_info();
    auto fileInfoExtended = response.mutable_file_info_extended();
    string message;
    bool success = fillExtendedFileInfo(fileInfoExtended, fileInfo, request.directory(), request.file(), request.hdu(), message);
    response.set_success(success);
    response.set_message(message);
    sendEvent("FILE_INFO_RESPONSE", requestId, response);
}

void Session::onOpenFile(const OpenFile& message, uint32_t requestId) {
    auto fileId(message.file_id());
    OpenFileAck ack;
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
        auto frame = unique_ptr<Frame>(new Frame(uuid, filename, hdu));
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

void Session::onCloseFile(const CloseFile& message, uint32_t requestId) {
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

void Session::onSetImageView(const SetImageView& message, uint32_t requestId) {
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
                    RasterImageData rasterImageData;
                    rasterImageData.set_stokes(frames.at(fileId)->currentStokes());
                    rasterImageData.set_channel(frames.at(fileId)->currentChannel());
                    if (newFrame) {
                        RegionHistogramData* histogramData = getRegionHistogramData(fileId, IMAGE_REGION_ID);
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
                    RegionHistogramData* histogramData = getRegionHistogramData(fileId, IMAGE_REGION_ID);
                    // RESPONSE: updated raster/histogram, spatial profile, spectral profile
                    RasterImageData rasterImageData;
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
    SetRegionAck ack;
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
                vector<string>(message.spatial_profiles().begin(), message.spatial_profiles().end()))) {
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
            if (frames.at(fileId)->setRegionHistogramRequirements(regionId, vector<CARTA::SetHistogramRequirements_HistogramConfig>(message.histograms().begin(), message.histograms().end()))) {
                // RESPONSE
                RegionHistogramData* histogramData = getRegionHistogramData(fileId, regionId);
                if (histogramData != nullptr) {
                    sendFileEvent(fileId, "REGION_HISTOGRAM_DATA", 0, *histogramData);
                } else {
                    string error = "Failed to load histogram data";
                    sendLogEvent(error, {"histogram"}, CARTA::ErrorSeverity::ERROR);
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
                vector<CARTA::SetSpectralRequirements_SpectralConfig>(message.spectral_profiles().begin(),
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
            if (frames.at(fileId)->setRegionStatsRequirements(regionId, vector<int>(message.stats().begin(),
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
    RegionHistogramData* histogramMessage(nullptr);
    if (frames.count(fileId)) {
        try {
            histogramMessage = new RegionHistogramData();
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
                if (compressionType == CompressionType::NONE) {
                    rasterImageData.set_compression_type(CompressionType::NONE);
                    rasterImageData.set_compression_quality(0);
                    rasterImageData.add_image_data(imageData.data(), imageData.size() * sizeof(float));
                    // Send completed event to client
                    sendFileEvent(fileId, "RASTER_IMAGE_DATA", 0, rasterImageData);
                } else if (compressionType == CompressionType::ZFP) {
                    int precision = lround(compression.quality);
                    rasterImageData.set_compression_type(CompressionType::ZFP);
                    rasterImageData.set_compression_quality(precision);

                    auto rowLength = (bounds.x_max() - bounds.x_min()) / mip;
                    auto numRows = (bounds.y_max() - bounds.y_min()) / mip;
                    auto numSubsets = compression.nsubsets;

                    vector<vector<char>> compressionBuffers(numSubsets);
                    vector<size_t> compressedSizes(numSubsets);
                    vector<vector<int32_t>> nanEncodings(numSubsets);

                    auto N = min(numSubsets, MAX_SUBSETS);
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
                    auto tStartCompress = chrono::high_resolution_clock::now();
                    tbb::parallel_for(range, loop);
                    auto tEndCompress = chrono::high_resolution_clock::now();
                    auto dtCompress = chrono::duration_cast<chrono::microseconds>(tEndCompress - tStartCompress).count();

                    // Complete message
                    for (auto i = 0; i < numSubsets; i++) {
                        rasterImageData.add_image_data(compressionBuffers[i].data(), compressedSizes[i]);
                        rasterImageData.add_nan_encodings((char*) nanEncodings[i].data(), nanEncodings[i].size() * sizeof(int));
                    }

                    if (verboseLogging) {
                        string compressionInfo = fmt::format(
                            "Image data of size {:.1f} kB compressed to {:.1f} kB in {} ms at {:.2f} MPix/s",
                            numRows * rowLength * sizeof(float) / 1e3,
                            accumulate(compressedSizes.begin(), compressedSizes.end(), 0) * 1e-3,
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
    std::copy_n(eventName.begin(), min(eventName.length(), eventNameLength), msg.begin());
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
