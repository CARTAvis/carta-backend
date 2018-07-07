#include "Session.h"
#include <carta-protobuf/raster_image.pb.h>
#include <carta-protobuf/region_histogram.pb.h>
using namespace H5;
using namespace std;
using namespace CARTA;
namespace fs = boost::filesystem;

// Default constructor. Associates a websocket with a UUID and sets the base folder for all files
Session::Session(uWS::WebSocket<uWS::SERVER>* ws, boost::uuids::uuid uuid, map<string, vector<string>>& permissionsMap, string folder, ctpl::thread_pool& serverThreadPool, bool verbose)
    : uuid(uuid),
      permissionsMap(permissionsMap),
      baseFolder(folder),
      verboseLogging(verbose),
      threadPool(serverThreadPool),
      rateSum(0),
      rateCount(0),
      socket(ws) {
}

Session::~Session() {

}

bool Session::checkPermissionForEntry(string entry) {
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

FileListResponse Session::getFileList(string folder) {
    string fullPath = baseFolder;
    // constructs the full path based on the base folder and the folder string (unless folder is empty or root)
    if (folder.length() && folder != "/") {
        fullPath = fmt::format("{}/{}", baseFolder, folder);
    }
    fs::path folderPath(fullPath);
    FileListResponse fileList;
    if (folder.length() && folder != "/") {
        fileList.set_directory(folder);
        auto lastSlashPosition = folder.find_last_of('/');
        if (lastSlashPosition != string::npos) {
            fileList.set_parent(folder.substr(0, lastSlashPosition));
        } else {
            fileList.set_parent("/");
        }
    }

    string message;

    try {
        if (checkPermissionForDirectory(folder) && fs::exists(folderPath) && fs::is_directory(folderPath)) {
            for (auto& directoryEntry : fs::directory_iterator(folderPath)) {
                fs::path filePath(directoryEntry);
                string filenameString = directoryEntry.path().filename().string();
                // Check if it is a directory and the user has permission to access it
                string pathNameRelative = (folder.length() && folder != "/") ? folder + "/" + filenameString : filenameString;
                if (fs::is_directory(filePath) && checkPermissionForDirectory(pathNameRelative)) {
                    fileList.add_subdirectories(filenameString);
                }
                    // Check if it is an HDF5 file
                else if (fs::is_regular_file(filePath) && H5File::isHdf5(filePath.string())) {
                    auto fileInfo = fileList.add_files();
                    if (!fillFileInfo(fileInfo, filePath, message)) {
                        fileList.set_success(false);
                        fileList.set_message(message);
                        return fileList;
                    }
                }
            }
        }
    }
    catch (const fs::filesystem_error& ex) {
        fmt::print("Error: {}\n", ex.what());
        fileList.set_success(false);
        fileList.set_message(ex.what());
        return fileList;
    }

    fileList.set_success(true);
    return fileList;
}

bool Session::fillFileInfo(FileInfo* fileInfo, fs::path& path, string& message) {
    string filenameString = path.filename().string();
    fileInfo->set_size(fs::file_size(path));
    fileInfo->set_name(filenameString);
    fileInfo->set_type(FileType::HDF5);
    H5File file(path.string(), H5F_ACC_RDONLY);
    auto N = file.getNumObjs();
    for (auto i = 0; i < N; i++) {
        if (file.getObjTypeByIdx(i) == H5G_GROUP) {
            string groupName = file.getObjnameByIdx(i);
            fileInfo->add_hdu_list(groupName);
        }
    }
    return fileInfo->hdu_list_size() > 0;
}

bool Session::fillExtendedFileInfo(FileInfoExtended* extendedInfo, FileInfo* fileInfo, const string folder, const string filename, string hdu, string& message) {
    string pathString;
    // constructs the full path based on the base folder, the folder string and the filename
    if (folder.length()) {
        pathString = fmt::format("{}/{}/{}", baseFolder, folder, filename);
    } else {
        pathString = fmt::format("{}/{}", baseFolder, filename);
    }

    fs::path filePath(pathString);

    try {
        if (fs::is_regular_file(filePath) && H5File::isHdf5(filePath.string())) {
            if (!fillFileInfo(fileInfo, filePath, message)) {
                return false;
            }

            // Add extended info
            H5File file(filePath.string(), H5F_ACC_RDONLY);
            bool hasHDU;
            if (hdu.length()) {
                hasHDU = H5Lexists(file.getId(), hdu.c_str(), 0);
            } else {
                auto N = file.getNumObjs();
                hasHDU = false;
                for (auto i = 0; i < N; i++) {
                    if (file.getObjTypeByIdx(i) == H5G_GROUP) {
                        hdu = file.getObjnameByIdx(i);
                        hasHDU = true;
                        break;
                    }
                }
            }

            if (hasHDU) {
                H5::Group topLevelGroup = file.openGroup(hdu);
                if (H5Lexists(topLevelGroup.getId(), "DATA", 0)) {
                    DataSet dataSet = topLevelGroup.openDataSet("DATA");
                    vector<hsize_t> dims(dataSet.getSpace().getSimpleExtentNdims(), 0);
                    dataSet.getSpace().getSimpleExtentDims(dims.data(), NULL);
                    uint32_t N = dims.size();
                    extendedInfo->set_dimensions(N);
                    if (N < 2 || N > 4) {
                        message = "Image must be 2D, 3D or 4D.";
                        return false;
                    }
                    extendedInfo->set_width(dims[N - 1]);
                    extendedInfo->set_height(dims[N - 2]);
                    extendedInfo->set_depth((N > 2) ? dims[N - 3] : 1);
                    extendedInfo->set_stokes((N > 3) ? dims[N - 4] : 1);

                    H5O_info_t groupInfo;
                    H5Oget_info(topLevelGroup.getId(), &groupInfo);
                    for (auto i = 0; i < groupInfo.num_attrs; i++) {
                        Attribute attr = topLevelGroup.openAttribute(i);
                        hid_t attrTypeId = H5Aget_type(attr.getId());
                        auto headerEntry = extendedInfo->add_header_entries();
                        headerEntry->set_name(attr.getName());

                        auto typeClass = H5Tget_class(attrTypeId);
                        if (typeClass == H5T_STRING) {
                            attr.read(attr.getStrType(), *headerEntry->mutable_value());
                            headerEntry->set_entry_type(EntryType::STRING);
                        } else if (typeClass == H5T_INTEGER) {
                            int64_t valueInt;
                            DataType intType(PredType::NATIVE_INT64);
                            attr.read(intType, &valueInt);
                            *headerEntry->mutable_value() = fmt::format("{}", valueInt);
                            headerEntry->set_numeric_value(valueInt);
                            headerEntry->set_entry_type(EntryType::INT);
                        } else if (typeClass == H5T_FLOAT) {
                            DataType doubleType(PredType::NATIVE_DOUBLE);
                            double numericValue = 0;
                            attr.read(doubleType, &numericValue);
                            headerEntry->set_numeric_value(numericValue);
                            headerEntry->set_entry_type(EntryType::FLOAT);
                            *headerEntry->mutable_value() = fmt::format("{:f}", numericValue);
                        }
                    }
                } else {
                    message = "File is missing DATA dataset";
                    return false;
                }
            } else {
                message = "File is missing top-level group";
                return false;
            }
        } else {
            message = "File is not a valid HDF5 file";
            return false;
        }
    }
    catch (const fs::filesystem_error& ex) {
        message = ex.what();
        return false;
    }
    return true;
}

// CARTA ICD implementation
void Session::onRegisterViewer(const RegisterViewer& message, uint64_t requestId) {
    apiKey = message.api_key();
    RegisterViewerAck ackMessage;
    ackMessage.set_success(true);
    ackMessage.set_session_id(boost::uuids::to_string(uuid));
    sendEvent("REGISTER_VIEWER_ACK", requestId, ackMessage);
}

void Session::onFileListRequest(const FileListRequest& request, uint64_t requestId) {
    string folder = request.directory();
    if (folder.length() > 1 && folder[0] == '/') {
        folder = folder.substr(1);
    }
    FileListResponse response = getFileList(folder);
    sendEvent("FILE_LIST_RESPONSE", requestId, response);
}

void Session::onFileInfoRequest(const FileInfoRequest& request, uint64_t requestId) {
    FileInfoResponse response;
    auto fileInfo = response.mutable_file_info();
    auto fileInfoExtended = response.mutable_file_info_extended();
    string message;
    bool success = fillExtendedFileInfo(fileInfoExtended, fileInfo, request.directory(), request.file(), request.hdu(), message);
    response.set_success(success);
    response.set_message(message);
    sendEvent("FILE_INFO_RESPONSE", requestId, response);
}

void Session::onOpenFile(const OpenFile& message, uint64_t requestId) {
    OpenFileAck ack;
    auto fileInfo = ack.mutable_file_info();
    auto fileInfoExtended = ack.mutable_file_info_extended();
    string errMessage;
    bool infoSuccess = fillExtendedFileInfo(fileInfoExtended, fileInfo, message.directory(), message.file(), message.hdu(), errMessage);
    if (infoSuccess && fileInfo->hdu_list_size()) {
        string filename;
        if (message.directory().length() && message.directory() != "/") {
            filename = fmt::format("{}/{}/{}", baseFolder, message.directory(), message.file());
        } else {
            filename = fmt::format("{}/{}", baseFolder, message.file());
        }
        string hdu = fileInfo->hdu_list(0);

        auto frame = unique_ptr<Frame>(new Frame(boost::uuids::to_string(uuid), filename, hdu));
        if (frame->isValid()) {
            ack.set_success(true);
            frames[message.file_id()] = move(frame);
        } else {
            ack.set_success(false);
            ack.set_message("Could not load file");
        }
    } else {
        ack.set_success(false);
        ack.set_message(errMessage);
    }
    sendEvent("OPEN_FILE_ACK", requestId, ack);

    // Send histogram of the default channel
    if (ack.success()) {
        RegionHistogramData histogramMessage;
        histogramMessage.set_file_id(message.file_id());
        histogramMessage.set_stokes(frames[message.file_id()]->currentStokes());
        // -1 corresponds to the entire current XY plane
        histogramMessage.set_region_id(-1);
        histogramMessage.mutable_histograms()->AddAllocated(new Histogram(frames[message.file_id()]->currentHistogram()));
        sendEvent("REGION_HISTOGRAM_DATA", 0, histogramMessage);
    }
}

void Session::onSetImageView(const SetImageView& message, uint64_t requestId) {
    RasterImageData rasterImageData;
    // Check if frame is loaded
    if (frames.count(message.file_id())) {
        auto& frame = frames[message.file_id()];
        auto imageData = frame->getImageData(message.image_bounds(), message.mip());
        // Check if image data is valid
        if (!imageData.empty()) {
            rasterImageData.set_file_id(message.file_id());
            rasterImageData.set_stokes(frame->currentStokes());
            rasterImageData.set_channel(frame->currentChannel());
            rasterImageData.set_mip(message.mip());
            // Uncompressed only for now
            rasterImageData.set_compression_type(CompressionType::NONE);
            rasterImageData.set_compression_quality(0);
            rasterImageData.set_num_subsets(1);
            // Copy over image bounds
            rasterImageData.mutable_image_bounds()->set_x_min(message.image_bounds().x_min());
            rasterImageData.mutable_image_bounds()->set_x_max(message.image_bounds().x_max());
            rasterImageData.mutable_image_bounds()->set_y_min(message.image_bounds().y_min());
            rasterImageData.mutable_image_bounds()->set_y_max(message.image_bounds().y_max());
            // Copy image data and send event
            rasterImageData.add_image_data(imageData.data(), imageData.size() * sizeof(float));
            sendEvent("RASTER_IMAGE_DATA", requestId, rasterImageData);
        }
    } else {
        // TODO: error handling
    }

}

// Sends an event to the client with a given event name (padded/concatenated to 32 characters) and a given ProtoBuf message
void Session::sendEvent(string eventName, u_int64_t eventId, google::protobuf::MessageLite& message) {
    size_t eventNameLength = 32;
    int messageLength = message.ByteSize();
    size_t requiredSize = eventNameLength + 8 + messageLength;
    if (binaryPayloadCache.size() < requiredSize) {
        binaryPayloadCache.resize(requiredSize);
    }
    memset(binaryPayloadCache.data(), 0, eventNameLength);
    memcpy(binaryPayloadCache.data(), eventName.c_str(), min(eventName.length(), eventNameLength));
    memcpy(binaryPayloadCache.data() + eventNameLength, &eventId, 4);
    message.SerializeToArray(binaryPayloadCache.data() + eventNameLength + 8, messageLength);
    socket->send(binaryPayloadCache.data(), requiredSize, uWS::BINARY);
}
