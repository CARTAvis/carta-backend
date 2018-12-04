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
      outgoing(outgoing) {
}

Session::~Session() {
    for (auto& frame : frames) {
        frame.second.reset();
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
                casacore::File ccfile(dirIter.file());  // casacore File
                casacore::String fullpath(ccfile.path().absoluteName());
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


// ********************************************************************************
// Histogram message; sent separately or within RasterImageData

CARTA::RegionHistogramData* Session::getRegionHistogramData(const int32_t fileId, const int32_t regionId) {
    RegionHistogramData* histogramMessage(nullptr);
    if (frames.count(fileId)) {
        auto& frame = frames[fileId];
        histogramMessage = new RegionHistogramData();
        histogramMessage->set_file_id(fileId);
        histogramMessage->set_region_id(regionId);
        frame->fillRegionHistogramData(regionId, histogramMessage);
    }
    return histogramMessage;
}

// ********************************************************************************
// Compress data

void Session::setCompression(CARTA::CompressionType type, float quality, int nsubsets) {
    compressionSettings.type = type;
    compressionSettings.quality = quality;
    compressionSettings.nsubsets = nsubsets;
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
    OpenFileAck ack;
    auto fileId(message.file_id());
    ack.set_file_id(fileId);
    auto fileInfo = ack.mutable_file_info();
    auto fileInfoExtended = ack.mutable_file_info_extended();
    string errMessage;
    bool infoSuccess = fillExtendedFileInfo(fileInfoExtended, fileInfo, message.directory(), message.file(), message.hdu(), errMessage);
    if (infoSuccess && fileInfo->hdu_list_size()) {
        // form filename with path
        casacore::Path path(baseFolder);
        path.append(message.directory());
        path.append(message.file());
        string filename(path.absoluteName());
        // create Frame for open file
        string hdu = fileInfo->hdu_list(0);
        auto frame = unique_ptr<Frame>(new Frame(uuid, filename, hdu));
        if (frame->isValid()) {
            ack.set_success(true);
            frames[fileId] = move(frame);
        } else {
            ack.set_success(false);
            ack.set_message("Could not load file");
        }
    } else {
        ack.set_success(false);
        ack.set_message(errMessage);
    }
    sendEvent("OPEN_FILE_ACK", requestId, ack);
}

void Session::onCloseFile(const CloseFile& message, uint32_t requestId) {
    auto fileId = message.file_id();
    if (fileId == -1) {
        for (auto& frame : frames) {
            frame.second.reset();
        }
        frames.clear();
    } else if (frames.count(fileId)) {
        frames[fileId].reset();
    }
}

void Session::onSetImageView(const SetImageView& message, uint32_t requestId) {
    auto fileId = message.file_id();
    if (frames.count(fileId)) {
        auto& frame = frames[fileId];
        // set new view in Frame
        if (frame->setBounds(message.image_bounds(), message.mip())) {
            // save compression settings for sending raster data
            CARTA::CompressionType ctype(message.compression_type());
            int numsets(message.num_subsets());
            float quality(message.compression_quality());
            setCompression(ctype, quality, numsets);
            // RESPONSE
            CARTA::RegionHistogramData* histogramData = getRegionHistogramData(fileId, IMAGE_REGION_ID);
            sendRasterImageData(fileId, requestId, histogramData);
        } else {
            string error = "Image bounds out of range; cannot update image";
            sendLogEvent(error, {"view"}, CARTA::ErrorSeverity::ERROR);
        }
    } else {
        string error = fmt::format("File id {} not found", fileId);
        sendLogEvent(error, {"view"}, CARTA::ErrorSeverity::DEBUG);
    }
}

void Session::onSetImageChannels(const CARTA::SetImageChannels& message, uint32_t requestId) {
    auto fileId(message.file_id());
    if (frames.count(fileId)) {
        auto& frame = frames[fileId];
        size_t newChannel(message.channel()), newStokes(message.stokes());
        bool channelChanged(newChannel != frame->currentChannel()),
             stokesChanged(newStokes != frame->currentStokes());
        if (channelChanged || stokesChanged) {
            string errMessage;
            if (frame->setImageChannels(message.channel(), message.stokes(), errMessage)) {
                // RESPONSE: updated histogram, spatial profile, spectral profile
                // Histogram included in the raster image data message
                RegionHistogramData* histogramData = getRegionHistogramData(fileId, IMAGE_REGION_ID);
                sendRasterImageData(fileId, requestId, histogramData);
                sendSpatialProfileData(fileId, CURSOR_REGION_ID);
                if (stokesChanged)
                    sendSpectralProfileData(fileId, CURSOR_REGION_ID);
            } else {
                sendLogEvent(errMessage, {"channels"}, CARTA::ErrorSeverity::ERROR);
            }
        }
    } else {
        string error = fmt::format("File id {} not found", fileId);
        sendLogEvent(error, {"channels"}, CARTA::ErrorSeverity::DEBUG);
    }
}

void Session::onSetCursor(const CARTA::SetCursor& message, uint32_t requestId) {
    auto fileId(message.file_id());
    if (frames.count(fileId)) {
        auto& frame = frames[fileId];
        if (frame->setCursorRegion(CURSOR_REGION_ID, message.point())) {
            if (message.has_spatial_requirements()) {
                onSetSpatialRequirements(message.spatial_requirements(), requestId);
                sendSpectralProfileData(fileId, CURSOR_REGION_ID);
            } else {
                // RESPONSE
                sendSpatialProfileData(fileId, CURSOR_REGION_ID);
                sendSpectralProfileData(fileId, CURSOR_REGION_ID);
            }
        } else {
            string error = "Cursor point out of range";
            sendLogEvent(error, {"cursor"}, CARTA::ErrorSeverity::ERROR);
        }
    } else {
        string error = fmt::format("File id {} not found", fileId);
        sendLogEvent(error, {"cursor"}, CARTA::ErrorSeverity::DEBUG);
    }
}

void Session::onSetRegion(const CARTA::SetRegion& message, uint32_t requestId) {
    SetRegionAck ack;
    auto fileId(message.file_id());
    auto regionId(message.region_id());
    ack.set_region_id(regionId);
    if (frames.count(fileId)) {
        if (message.region_id() <= 0) { // get region id unique across all frames
            for (auto& frame : frames) // frames = map<fileId, unique_ptr<Frame>>
                regionId = std::max(regionId, frame.second->getMaxRegionId());
            ++regionId; // get next available
            if (regionId == 0) // reserved for cursor
                ++regionId;
        }
        std::vector<int> stokes = {message.stokes().begin(), message.stokes().end()};
        std::vector<CARTA::Point> points = {message.control_points().begin(), message.control_points().end()};
        std::string errMessage;
        auto& frame = frames[fileId];  // use frame in SetRegion message
        bool success = frame->setRegion(regionId, message.region_name(), message.region_type(), message.channel_min(),
            message.channel_max(), stokes, points, message.rotation(), errMessage);
        ack.set_success(success);
        ack.set_message(errMessage);
        ack.set_region_id(regionId);
    } else {
        ack.set_success(false);
        ack.set_message("Invalid file id");
    }
    // RESPONSE
    sendEvent("SET_REGION_ACK", requestId, ack);
}

void Session::onRemoveRegion(const CARTA::RemoveRegion& message, uint32_t requestId) {
    auto regionId(message.region_id());
    for (auto& frame : frames)  // frames = map<fileId, unique_ptr<Frame>>
        frame.second->removeRegion(regionId);
}

void Session::onSetSpatialRequirements(const CARTA::SetSpatialRequirements& message, uint32_t requestId) {
    auto fileId(message.file_id());
    if (frames.count(fileId)) {
        auto& frame = frames[fileId];
        auto regionId = message.region_id();
        if (frame->setRegionSpatialRequirements(regionId, vector<string>(message.spatial_profiles().begin(),
            message.spatial_profiles().end()))) {
            // RESPONSE
            sendSpatialProfileData(fileId, regionId);
        } else {
            string error = fmt::format("Spatial requirements for region id {} failed to validate ", regionId);
            sendLogEvent(error, {"spatial"}, CARTA::ErrorSeverity::ERROR);
        }
    } else {
        string error = fmt::format("File id {} not found", fileId);
        sendLogEvent(error, {"spatial"}, CARTA::ErrorSeverity::DEBUG);
    }
}

void Session::onSetHistogramRequirements(const CARTA::SetHistogramRequirements& message, uint32_t requestId) {
    auto fileId(message.file_id());
    if (frames.count(fileId)) {
        auto& frame = frames[fileId];
        auto regionId = message.region_id();
        if (frame->setRegionHistogramRequirements(regionId, vector<CARTA::SetHistogramRequirements_HistogramConfig>(message.histograms().begin(), message.histograms().end()))) {
            // RESPONSE
            RegionHistogramData* histogramData = getRegionHistogramData(fileId, regionId);
            if (histogramData != nullptr) {
                sendFileEvent(fileId, "REGION_HISTOGRAM_DATA", requestId, *histogramData);
            } else {
                string error = "Failed to load histogram data";
                sendLogEvent(error, {"histogram"}, CARTA::ErrorSeverity::ERROR);
            }
        } else {
            string error = fmt::format("Histogram requirements for region id {} failed to validate ", regionId);
            sendLogEvent(error, {"histogram"}, CARTA::ErrorSeverity::ERROR);
        }
    } else {
        string error = fmt::format("File id {} not found", fileId);
        sendLogEvent(error, {"histogram"}, CARTA::ErrorSeverity::DEBUG);
    }
}

void Session::onSetSpectralRequirements(const CARTA::SetSpectralRequirements& message, uint32_t requestId) {
    auto fileId(message.file_id());
    if (frames.count(fileId)) {
        auto& frame = frames[fileId];
        auto regionId = message.region_id();
        if (frame->setRegionSpectralRequirements(regionId,
            vector<CARTA::SetSpectralRequirements_SpectralConfig>(message.spectral_profiles().begin(),
            message.spectral_profiles().end()))) {
            // RESPONSE
            sendSpectralProfileData(fileId, regionId);
        } else {
            string error = fmt::format("Spectral requirements for region id {} failed to validate ", regionId);
            sendLogEvent(error, {"spectral"}, CARTA::ErrorSeverity::ERROR);
        }
    } else {
        string error = fmt::format("File id {} not found", fileId);
        sendLogEvent(error, {"spectral"}, CARTA::ErrorSeverity::DEBUG);
    }
}

void Session::onSetStatsRequirements(const CARTA::SetStatsRequirements& message, uint32_t requestId) {
    auto fileId(message.file_id());
    if (frames.count(fileId)) {
        auto& frame = frames[fileId];
        auto regionId = message.region_id();
        if (frame->setRegionStatsRequirements(regionId, vector<int>(message.stats().begin(), message.stats().end()))) {
            // RESPONSE
            sendRegionStatsData(fileId, regionId);
        } else {
            string error = fmt::format("Stats requirements for region id {} failed to validate ", regionId);
            sendLogEvent(error, {"stats"}, CARTA::ErrorSeverity::ERROR);
        }
    } else {
        string error = fmt::format("File id {} not found", fileId);
        sendLogEvent(error, {"stats"}, CARTA::ErrorSeverity::DEBUG);
    }
}

// ******** SEND DATA STREAMS *********

void Session::sendRasterImageData(int fileId, uint32_t requestId, CARTA::RegionHistogramData* channelHistogram) {
    RasterImageData rasterImageData;
    // Add histogram, if it exists
    if (channelHistogram) {
        rasterImageData.set_allocated_channel_histogram_data(channelHistogram);
    }
    if (frames.count(fileId)) {
        auto& frame = frames[fileId];
        auto imageData = frame->getImageData();
        // Check if image data is valid
        if (!imageData.empty()) {
            rasterImageData.set_file_id(fileId);
            rasterImageData.set_stokes(frame->currentStokes());
            rasterImageData.set_channel(frame->currentChannel());
            rasterImageData.set_mip(frame->currentMip());
            // Copy over image bounds
            auto imageBounds = frame->currentBounds();
            auto mip = frame->currentMip();
            rasterImageData.mutable_image_bounds()->set_x_min(imageBounds.x_min());
            rasterImageData.mutable_image_bounds()->set_x_max(imageBounds.x_max());
            rasterImageData.mutable_image_bounds()->set_y_min(imageBounds.y_min());
            rasterImageData.mutable_image_bounds()->set_y_max(imageBounds.y_max());

            auto compressionType = compressionSettings.type;
            if (compressionType == CompressionType::NONE) {
                rasterImageData.set_compression_type(CompressionType::NONE);
                rasterImageData.set_compression_quality(0);
                rasterImageData.add_image_data(imageData.data(), imageData.size() * sizeof(float));
            } else if (compressionType == CompressionType::ZFP) {

                int precision = lround(compressionSettings.quality);
                auto rowLength = (imageBounds.x_max() - imageBounds.x_min()) / mip;
                auto numRows = (imageBounds.y_max() - imageBounds.y_min()) / mip;
                rasterImageData.set_compression_type(CompressionType::ZFP);
                rasterImageData.set_compression_quality(precision);

                auto numSubsets(compressionSettings.nsubsets);
                vector<char> compressionBuffers[numSubsets];
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
                        nanEncodings[i] = getNanEncodingsBlock(imageData, subsetElementStart, rowLength, subsetRowEnd - subsetRowStart);
                        compress(imageData, subsetElementStart, compressionBuffers[i], compressedSizes[i], rowLength, subsetRowEnd - subsetRowStart, precision);
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
                    string compressionInfo = fmt::format("Image data of size {:.1f} kB compressed to {:.1f} kB in {} ms at {:.2f} MPix/s",
                               numRows * rowLength * sizeof(float) / 1e3,
                               accumulate(compressedSizes.begin(), compressedSizes.end(), 0) * 1e-3,
                               1e-3 * dtCompress,
                               (float) (numRows * rowLength) / dtCompress);
                    log(uuid, compressionInfo);
                    sendLogEvent(compressionInfo, {"zfp"}, CARTA::ErrorSeverity::DEBUG);
                }
            } else {
                // TODO: error handling for SZ
            }
            // Send completed event to client
            sendFileEvent(fileId, "RASTER_IMAGE_DATA", requestId, rasterImageData);
        } else {
            string error = "Raster image data failed to load";
            sendLogEvent(error, {"raster"}, CARTA::ErrorSeverity::ERROR);
        }
    }
}

void Session::sendSpatialProfileData(int fileId, int regionId) {
    if (frames.count(fileId)) {
        auto& frame = frames[fileId];
        CARTA::SpatialProfileData spatialProfileData;
        if (frame->fillSpatialProfileData(regionId, spatialProfileData)) {
            spatialProfileData.set_file_id(fileId);
            spatialProfileData.set_region_id(regionId);
            sendFileEvent(fileId, "SPATIAL_PROFILE_DATA", 0, spatialProfileData);
        } else {
            string error = "Spatial profile data failed to load";
            sendLogEvent(error, {"spatial"}, CARTA::ErrorSeverity::ERROR);
        }
    } else {
        string error = fmt::format("File id {} not found", fileId);
        sendLogEvent(error, {"spatial"}, CARTA::ErrorSeverity::DEBUG);
    }
}

void Session::sendSpectralProfileData(int fileId, int regionId) {
    if (frames.count(fileId)) {
        auto& frame = frames[fileId];
        CARTA::SpectralProfileData spectralProfileData;
        if (frame->fillSpectralProfileData(regionId, spectralProfileData)) {
            spectralProfileData.set_file_id(fileId);
            spectralProfileData.set_region_id(regionId);
            sendFileEvent(fileId, "SPECTRAL_PROFILE_DATA", 0, spectralProfileData);
        } else {
            string error = "Spectral profile data failed to load";
            sendLogEvent(error, {"spectral"}, CARTA::ErrorSeverity::ERROR);
        }
    } else {
        string error = fmt::format("File id {} not found", fileId);
        sendLogEvent(error, {"spectral"}, CARTA::ErrorSeverity::DEBUG);
    }
}

void Session::sendRegionStatsData(int fileId, int regionId) {
    if (frames.count(fileId)) {
        auto& frame = frames[fileId];
        CARTA::RegionStatsData regionStatsData;
        if (frame->fillRegionStatsData(regionId, regionStatsData)) {
            regionStatsData.set_file_id(fileId);
            regionStatsData.set_region_id(regionId);
            sendFileEvent(fileId, "REGION_STATS_DATA", 0, regionStatsData);
        } else {
            string error = "Region stats data failed to load";
            sendLogEvent(error, {"stats"}, CARTA::ErrorSeverity::ERROR);
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
