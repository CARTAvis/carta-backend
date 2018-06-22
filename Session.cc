#include "Session.h"
#include <proto/fileLoadResponse.pb.h>
#include <proto/connectionResponse.pb.h>
#include <proto/profileResponse.pb.h>
#include <proto/regionStatsResponse.pb.h>

using namespace H5;
using namespace std;
namespace fs = boost::filesystem;

// Default constructor. Associates a websocket with a UUID and sets the base folder for all files
Session::Session(uWS::WebSocket<uWS::SERVER>* ws, boost::uuids::uuid uuid, map<string, vector<string>>& permissionsMap, string folder, ctpl::thread_pool& serverThreadPool, bool verbose)
    : uuid(uuid),
      permissionsMap(permissionsMap),
      currentChannel(-1),
      currentStokes(-1),
      file(nullptr),
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

CARTA::FileListResponse Session::getFileList(string folder) {
    string fullPath = baseFolder;
    // constructs the full path based on the base folder and the folder string (unless folder is empty or root)
    if (folder.length() && folder != "/") {
        fullPath = fmt::format("{}/{}", baseFolder, folder);
    }
    fs::path folderPath(fullPath);
    CARTA::FileListResponse fileList;
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

bool Session::fillFileInfo(CARTA::FileInfo* fileInfo, fs::path& path, string& message) {
    string filenameString = path.filename().string();
    fileInfo->set_size(fs::file_size(path));
    fileInfo->set_name(filenameString);
    fileInfo->set_type(CARTA::FileType::HDF5);
    H5File file(path.string(), H5F_ACC_RDONLY);
    auto N = file.getNumObjs();
    for (auto i = 0; i < N; i++) {
        if (file.getObjTypeByIdx(i) == H5G_GROUP) {
            string groupName = file.getObjnameByIdx(i);
            fileInfo->add_hdu_list(groupName);
        }
    }
    return fileInfo->hdu_list_size()>0;
}

bool Session::fillExtendedFileInfo(CARTA::FileInfoExtended* extendedInfo, CARTA::FileInfo* fileInfo, const string folder, const string filename, string hdu, string& message) {
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
            if (hdu.length()){
                hasHDU =  H5Lexists(file.getId(), hdu.c_str(), 0);
            }
            else {
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
                            headerEntry->set_entry_type(CARTA::EntryType::STRING);
                        } else if (typeClass == H5T_INTEGER) {
                            int64_t valueInt;
                            DataType intType(PredType::NATIVE_INT64);
                            attr.read(intType, &valueInt);
                            *headerEntry->mutable_value() = fmt::format("{}", valueInt);
                            headerEntry->set_numeric_value(valueInt);
                            headerEntry->set_entry_type(CARTA::EntryType::INT);
                        } else if (typeClass == H5T_FLOAT) {
                            DataType doubleType(PredType::NATIVE_DOUBLE);
                            double numericValue = 0;
                            attr.read(doubleType, &numericValue);
                            headerEntry->set_numeric_value(numericValue);
                            headerEntry->set_entry_type(CARTA::EntryType::FLOAT);
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

// Calculates channel histogram if it is not cached already
void Session::updateHistogram() {
    if (imageInfo.channelStats[currentStokes][currentChannel].histogramBins.size()) {
        return;
    }

    auto numRows = imageInfo.height;
    if (!numRows) {
        return;
    }
    auto rowSize = imageInfo.width;
    if (!rowSize) {
        return;
    }

    float minVal = currentChannelCache[0];
    float maxVal = currentChannelCache[0];
    float sum = 0.0f;
    int count = 0;
    for (auto i = 0; i < imageInfo.height; i++) {
        for (auto j = 0; j < imageInfo.width; j++) {
            auto v = currentChannelCache[i * imageInfo.width + j];
            minVal = fmin(minVal, v);
            maxVal = fmax(maxVal, v);
            sum += isnan(v) ? 0.0 : v;
            count += isnan(v) ? 0 : 1;
        }
    }

    ChannelStats& stats = imageInfo.channelStats[currentStokes][currentChannel];

    stats.minVal = minVal;
    stats.maxVal = maxVal;
    stats.nanCount = count;
    stats.mean = sum / max(count, 1);
    int N = int(max(sqrt(imageInfo.width * imageInfo.height), 2.0));
    stats.histogramBins.resize(N, 0);
    float binWidth = (stats.maxVal / stats.minVal) / N;

    for (auto i = 0; i < imageInfo.height; i++) {
        for (auto j = 0; j < imageInfo.width; j++) {
            auto v = currentChannelCache[i * imageInfo.width + j];
            if (isnan(v)) {
                continue;
            }
            int bin = min((int) ((v - minVal) / binWidth), N - 1);
            stats.histogramBins[bin]++;
        }
    }
    log("Cached histogram not found. Manually updated");
}

// Load statistics from file. This section of the code really needs some cleaning up. Should make an image abstract class, and implement
// 2D, 3D and 4D image subclasses for this process.
bool Session::loadStats() {
    if (!file) {
        log("No file loaded");
        return false;
    }

    imageInfo.channelStats.resize(imageInfo.stokes);
    for (auto i = 0; i < imageInfo.stokes; i++) {
        imageInfo.channelStats[i].resize(imageInfo.depth);
    }

    if (H5Lexists(file->getId(), "0/Statistics", 0) && H5Lexists(file->getId(), "0/Statistics/XY", 0)) {
        auto statsGroup = file->openGroup("0/Statistics/XY");
        auto group = file->openGroup("0");
        if (H5Lexists(statsGroup.getId(), "MAX", 0)) {
            auto dataSet = statsGroup.openDataSet("MAX");
            auto dataSpace = dataSet.getSpace();
            vector<hsize_t> dims(dataSpace.getSimpleExtentNdims(), 0);
            dataSpace.getSimpleExtentDims(dims.data(), NULL);

            // 2D cubes
            if (imageInfo.dimensions == 2 && dims.size() == 0) {
                dataSet.read(&imageInfo.channelStats[0][0].maxVal, PredType::NATIVE_FLOAT);
            } // 3D cubes
            else if (imageInfo.dimensions == 3 && dims.size() == 1 && dims[0] == imageInfo.depth) {
                vector<float> data(imageInfo.depth);
                dataSet.read(data.data(), PredType::NATIVE_FLOAT);
                for (auto i = 0; i < imageInfo.depth; i++) {
                    imageInfo.channelStats[0][i].maxVal = data[i];
                }
            } // 4D cubes
            else if (imageInfo.dimensions == 4 && dims.size() == 2 && dims[0] == imageInfo.stokes && dims[1] == imageInfo.depth) {
                vector<float> data(imageInfo.depth * imageInfo.stokes);
                dataSet.read(data.data(), PredType::NATIVE_FLOAT);
                for (auto i = 0; i < imageInfo.stokes; i++) {
                    for (auto j = 0; j < imageInfo.depth; j++) {
                        imageInfo.channelStats[i][j].maxVal = data[i * imageInfo.depth + j];
                    }
                }
            } else {
                log("Invalid MaxVals statistics");
                return false;
            }

        } else {
            log("Missing MaxVals statistics");
            return false;
        }

        if (H5Lexists(statsGroup.getId(), "MIN", 0)) {
            auto dataSet = statsGroup.openDataSet("MIN");
            auto dataSpace = dataSet.getSpace();
            vector<hsize_t> dims(dataSpace.getSimpleExtentNdims(), 0);
            dataSpace.getSimpleExtentDims(dims.data(), NULL);

            // 2D cubes
            if (imageInfo.dimensions == 2 && dims.size() == 0) {
                dataSet.read(&imageInfo.channelStats[0][0].minVal, PredType::NATIVE_FLOAT);
            } // 3D cubes
            else if (imageInfo.dimensions == 3 && dims.size() == 1 && dims[0] == imageInfo.depth) {
                vector<float> data(imageInfo.depth);
                dataSet.read(data.data(), PredType::NATIVE_FLOAT);
                for (auto i = 0; i < imageInfo.depth; i++) {
                    imageInfo.channelStats[0][i].minVal = data[i];
                }
            } // 4D cubes
            else if (imageInfo.dimensions == 4 && dims.size() == 2 && dims[0] == imageInfo.stokes && dims[1] == imageInfo.depth) {
                vector<float> data(imageInfo.stokes * imageInfo.depth);
                dataSet.read(data.data(), PredType::NATIVE_FLOAT);
                for (auto i = 0; i < imageInfo.stokes; i++) {
                    for (auto j = 0; j < imageInfo.depth; j++) {
                        imageInfo.channelStats[i][j].minVal = data[i * imageInfo.depth + j];
                    }
                }
            } else {
                log("Invalid MinVals statistics");
                return false;
            }

        } else {
            log("Missing MinVals statistics");
            return false;
        }

        if (H5Lexists(statsGroup.getId(), "MEAN", 0)) {
            auto dataSet = statsGroup.openDataSet("MEAN");
            auto dataSpace = dataSet.getSpace();
            vector<hsize_t> dims(dataSpace.getSimpleExtentNdims(), 0);
            dataSpace.getSimpleExtentDims(dims.data(), NULL);

            // 2D cubes
            if (imageInfo.dimensions == 2 && dims.size() == 0) {
                dataSet.read(&imageInfo.channelStats[0][0].mean, PredType::NATIVE_FLOAT);
            } // 3D cubes
            else if (imageInfo.dimensions == 3 && dims.size() == 1 && dims[0] == imageInfo.depth) {
                vector<float> data(imageInfo.depth);
                dataSet.read(data.data(), PredType::NATIVE_FLOAT);
                for (auto i = 0; i < imageInfo.depth; i++) {
                    imageInfo.channelStats[0][i].mean = data[i];
                }
            } // 4D cubes
            else if (imageInfo.dimensions == 4 && dims.size() == 2 && dims[0] == imageInfo.stokes && dims[1] == imageInfo.depth) {
                vector<float> data(imageInfo.stokes * imageInfo.depth);
                dataSet.read(data.data(), PredType::NATIVE_FLOAT);
                for (auto i = 0; i < imageInfo.stokes; i++) {
                    for (auto j = 0; j < imageInfo.depth; j++) {
                        imageInfo.channelStats[i][j].mean = data[i * imageInfo.depth + j];
                    }
                }
            } else {
                log("Invalid Means statistics");
                return false;
            }
        } else {
            log("Missing Means statistics");
            return false;
        }

        if (H5Lexists(statsGroup.getId(), "NAN_COUNT", 0)) {
            auto dataSet = statsGroup.openDataSet("NAN_COUNT");
            auto dataSpace = dataSet.getSpace();
            vector<hsize_t> dims(dataSpace.getSimpleExtentNdims(), 0);
            dataSpace.getSimpleExtentDims(dims.data(), NULL);

            // 2D cubes
            if (imageInfo.dimensions == 2 && dims.size() == 0) {
                dataSet.read(&imageInfo.channelStats[0][0].nanCount, PredType::NATIVE_INT64);
            } // 3D cubes
            else if (imageInfo.dimensions == 3 && dims.size() == 1 && dims[0] == imageInfo.depth) {
                vector<int64_t> data(imageInfo.depth);
                dataSet.read(data.data(), PredType::NATIVE_INT64);
                for (auto i = 0; i < imageInfo.depth; i++) {
                    imageInfo.channelStats[0][i].nanCount = data[i];
                }
            } // 4D cubes
            else if (imageInfo.dimensions == 4 && dims.size() == 2 && dims[0] == imageInfo.stokes && dims[1] == imageInfo.depth) {
                vector<int64_t> data(imageInfo.stokes * imageInfo.depth);
                dataSet.read(data.data(), PredType::NATIVE_INT64);
                for (auto i = 0; i < imageInfo.stokes; i++) {
                    for (auto j = 0; j < imageInfo.depth; j++) {
                        imageInfo.channelStats[i][j].nanCount = data[i * imageInfo.depth + j];
                    }
                }
            } else {
                log("Invalid NaNCounts statistics");
                return false;
            }
        } else {
            log("Missing NaNCounts statistics");
            return false;
        }

        if (H5Lexists(statsGroup.getId(), "HISTOGRAM", 0)) {
            auto dataSet = statsGroup.openDataSet("HISTOGRAM");
            auto dataSpace = dataSet.getSpace();
            vector<hsize_t> dims(dataSpace.getSimpleExtentNdims(), 0);
            dataSpace.getSimpleExtentDims(dims.data(), NULL);

            // 2D cubes
            if (imageInfo.dimensions == 2) {
                auto numBins = dims[0];
                vector<int> data(numBins);
                dataSet.read(data.data(), PredType::NATIVE_INT);
                imageInfo.channelStats[0][0].histogramBins = data;
            } // 3D cubes
            else if (imageInfo.dimensions == 3 && dims.size() == 2 && dims[0] == imageInfo.depth) {
                auto numBins = dims[1];
                vector<int> data(imageInfo.depth * numBins);
                dataSet.read(data.data(), PredType::NATIVE_INT);
                for (auto i = 0; i < imageInfo.depth; i++) {
                    imageInfo.channelStats[0][i].histogramBins.resize(numBins);
                    for (auto j = 0; j < numBins; j++) {
                        imageInfo.channelStats[0][i].histogramBins[j] = data[i * numBins + j];
                    }
                }
            } // 4D cubes
            else if (imageInfo.dimensions == 4 && dims.size() == 3 && dims[0] == imageInfo.stokes && dims[1] == imageInfo.depth) {
                auto numBins = dims[2];
                vector<int> data(imageInfo.stokes * imageInfo.depth * numBins);
                dataSet.read(data.data(), PredType::NATIVE_INT);
                for (auto i = 0; i < imageInfo.stokes; i++) {
                    for (auto j = 0; j < imageInfo.depth; j++) {
                        auto& stats = imageInfo.channelStats[i][j];
                        stats.histogramBins.resize(numBins);
                        for (auto k = 0; k < numBins; k++) {
                            stats.histogramBins[k] = data[(i * imageInfo.depth + j) * numBins + k];
                        }
                    }
                }
            } else {
                log("Invalid histogram statistics");
                return false;
            }

        } else {
            log("Missing Histograms group");
            return false;
        }
        if (H5Lexists(statsGroup.getId(), "PERCENTILES", 0) && H5Lexists(group.getId(), "PERCENTILE_RANKS", 0)) {
            auto dataSetPercentiles = statsGroup.openDataSet("PERCENTILES");
            auto dataSetPercentilesRank = group.openDataSet("PERCENTILE_RANKS");

            auto dataSpacePercentiles = dataSetPercentiles.getSpace();
            vector<hsize_t> dims(dataSpacePercentiles.getSimpleExtentNdims(), 0);
            dataSpacePercentiles.getSimpleExtentDims(dims.data(), NULL);
            auto dataSpaceRank = dataSetPercentilesRank.getSpace();
            vector<hsize_t> dimsRanks(dataSpaceRank.getSimpleExtentNdims(), 0);
            dataSpaceRank.getSimpleExtentDims(dimsRanks.data(), NULL);

            auto numRanks = dimsRanks[0];
            vector<float> ranks(numRanks);
            dataSetPercentilesRank.read(ranks.data(), PredType::NATIVE_FLOAT);

            if (imageInfo.dimensions == 2 && dims.size() == 1 && dims[0] == numRanks) {
                vector<float> vals(numRanks);
                dataSetPercentiles.read(vals.data(), PredType::NATIVE_FLOAT);
                imageInfo.channelStats[0][0].percentiles = vals;
                imageInfo.channelStats[0][0].percentileRanks = ranks;
            }
                // 3D cubes
            else if (imageInfo.dimensions == 3 && dims.size() == 2 && dims[0] == imageInfo.depth && dims[1] == numRanks) {
                vector<float> vals(imageInfo.depth * numRanks);
                dataSetPercentiles.read(vals.data(), PredType::NATIVE_FLOAT);

                for (auto i = 0; i < imageInfo.depth; i++) {
                    imageInfo.channelStats[0][i].percentileRanks = ranks;
                    imageInfo.channelStats[0][i].percentiles.resize(numRanks);
                    for (auto j = 0; j < numRanks; j++) {
                        imageInfo.channelStats[0][i].percentiles[j] = vals[i * numRanks + j];
                    }
                }
            }
                // 4D cubes
            else if (imageInfo.dimensions == 4 && dims.size() == 3 && dims[0] == imageInfo.stokes && dims[1] == imageInfo.depth && dims[2] == numRanks) {
                vector<float> vals(imageInfo.stokes * imageInfo.depth * numRanks);
                dataSetPercentiles.read(vals.data(), PredType::NATIVE_FLOAT);

                for (auto i = 0; i < imageInfo.stokes; i++) {
                    for (auto j = 0; j < imageInfo.depth; j++) {
                        auto& stats = imageInfo.channelStats[i][j];
                        stats.percentiles.resize(numRanks);
                        for (auto k = 0; k < numRanks; k++) {
                            stats.percentiles[k] = vals[(i * imageInfo.depth + j) * numRanks + k];
                        }
                        stats.percentileRanks = ranks;
                    }
                }
            } else {
                log("Missing Percentiles datasets");
                return false;
            }
        } else {
            log("Missing Percentiles group");
            return false;
        }

    } else {
        log("Missing Statistics group");
        return false;
    }

    return true;
}

// Loads a specific slice of the data cube
bool Session::loadChannel(int channel, int stokes) {
    if (!file) {
        log("No file loaded");
        return false;
    } else if (channel < 0 || channel >= imageInfo.depth || stokes < 0 || stokes >= imageInfo.stokes) {
        log("Channel {} (stokes {}) is invalid in file {}", channel, stokes, imageInfo.filename);
        return false;
    }

    // Define dimensions of hyperslab in 2D
    vector<hsize_t> count = {imageInfo.height, imageInfo.width};
    vector<hsize_t> start = {0, 0};

    // Append channel (and stokes in 4D) to hyperslab dims
    if (imageInfo.dimensions == 3) {
        count.insert(count.begin(), 1);
        start.insert(start.begin(), channel);
    } else if (imageInfo.dimensions == 4) {
        count.insert(count.begin(), {1, 1});
        start.insert(start.begin(), {stokes, channel});
    }

    // Read data into memory space
    hsize_t memDims[] = {imageInfo.height, imageInfo.width};
    DataSpace memspace(2, memDims);
    currentChannelCache.resize(imageInfo.width * imageInfo.height);
    auto sliceDataSpace = dataSets["main"].getSpace();
    sliceDataSpace.selectHyperslab(H5S_SELECT_SET, count.data(), start.data());
    dataSets["main"].read(currentChannelCache.data(), PredType::NATIVE_FLOAT, memspace, sliceDataSpace);

    currentStokes = stokes;
    currentChannel = channel;
    updateHistogram();
    return true;
}

// Loads a file and the default channel.
bool Session::loadFile(const string& filename, int defaultChannel) {
    string directory;
    auto lastSlash = filename.find_last_of('/');
    if (lastSlash == string::npos) {
        directory = "/";
    } else {
        directory = filename.substr(0, lastSlash);
    }

    // Check that the file is in an accessible directory
    if (!checkPermissionForDirectory(directory)) {
        return false;
    }

    if (filename == imageInfo.filename) {
        return true;
    }

    if (find(availableFileList.begin(), availableFileList.end(), filename) == availableFileList.end()) {
        log("Problem loading file {}: File is not in available file list.", filename);
        return false;
    }

    try {
        file.reset(new H5File(fmt::format("{}/{}", baseFolder, filename), H5F_ACC_RDONLY));

        imageInfo.filename = filename;
        auto group = file->openGroup("0");

        DataSet dataSet = group.openDataSet("DATA");
        vector<hsize_t> dims(dataSet.getSpace().getSimpleExtentNdims(), 0);
        dataSet.getSpace().getSimpleExtentDims(dims.data(), NULL);

        imageInfo.dimensions = dims.size();

        if (imageInfo.dimensions < 2 || imageInfo.dimensions > 4) {
            log("Problem loading file {}: Image must be 2D, 3D or 4D.", filename);
            return false;
        }

        imageInfo.width = dims[imageInfo.dimensions - 1];
        imageInfo.height = dims[imageInfo.dimensions - 2];
        imageInfo.depth = (imageInfo.dimensions > 2) ? dims[imageInfo.dimensions - 3] : 1;
        imageInfo.stokes = (imageInfo.dimensions > 3) ? dims[imageInfo.dimensions - 4] : 1;

        dataSets.clear();
        dataSets["main"] = dataSet;

        if (H5Lexists(group.getId(), "Statistics", 0) && H5Lexists(group.getId(), "Statistics/Z", 0) && H5Lexists(group.getId(), "Statistics/Z/MEAN", 0)) {
            dataSets["average"] = group.openDataSet("Statistics/Z/MEAN");
        }

        loadStats();

        // Swizzled data loaded if it exists. Used for Z-profiles and region stats
        if (H5Lexists(group.getId(), "SwizzledData", 0)) {
            if (imageInfo.dimensions == 3 && H5Lexists(group.getId(), "SwizzledData/ZYX", 0)) {
                DataSet dataSetSwizzled = group.openDataSet("SwizzledData/ZYX");
                vector<hsize_t> swizzledDims(dataSetSwizzled.getSpace().getSimpleExtentNdims(), 0);
                dataSetSwizzled.getSpace().getSimpleExtentDims(swizzledDims.data(), NULL);

                if (swizzledDims.size() != 3 || swizzledDims[0] != dims[2]) {
                    log("Invalid swizzled data set in file {}, ignoring.", filename);
                } else {
                    log("Found valid swizzled data set in file {}.", filename);
                    dataSets["swizzled"] = dataSetSwizzled;
                }
            } else if (imageInfo.dimensions == 4 && H5Lexists(group.getId(), "SwizzledData/ZYXW", 0)) {
                DataSet dataSetSwizzled = group.openDataSet("SwizzledData/ZYXW");
                vector<hsize_t> swizzledDims(dataSetSwizzled.getSpace().getSimpleExtentNdims(), 0);
                dataSetSwizzled.getSpace().getSimpleExtentDims(swizzledDims.data(), NULL);
                if (swizzledDims.size() != 4 || swizzledDims[1] != dims[3]) {
                    log("Invalid swizzled data set in file {}, ignoring.", filename);
                } else {
                    log("Found valid swizzled data set in file {}.", filename);
                    dataSets["swizzled"] = dataSetSwizzled;
                }
            } else {
                log("File {} missing optional swizzled data set, using fallback calculation.", filename);
            }
        } else {
            log("File {} missing optional swizzled data set, using fallback calculation.", filename);
        }
        return loadChannel(defaultChannel, 0);
    }
    catch (FileIException& err) {
        log("Problem loading file {}", filename);
        return false;
    }
}

// Calculates an X Profile for a given Y pixel coordinate and channel
vector<float> Session::getXProfile(int y, int channel, int stokes) {
    if (!file) {
        log("No file loaded or invalid session");
        return vector<float>();
    } else if (y < 0 || y >= imageInfo.height || channel < 0 || channel >= imageInfo.depth || stokes < 0 || stokes >= imageInfo.stokes) {
        log("X profile out of range");
        return vector<float>();
    }

    vector<float> profile;
    profile.resize(imageInfo.width);
    if ((channel == currentChannel && stokes == currentStokes) || imageInfo.dimensions == 2) {

        for (auto i = 0; i < imageInfo.width; i++) {
            profile[i] = currentChannelCache[y * imageInfo.width + i];
        }
        return profile;
    } else {
        try {
            // Defines dimensions of hyperslab in 3D
            vector<hsize_t> count = {1, 1, imageInfo.width};
            vector<hsize_t> start = {channel, y, 0};

            // Append stokes parameter to hyperslab dimensions in 4D
            if (imageInfo.dimensions == 4) {
                count.insert(count.begin(), 1);
                start.insert(start.begin(), stokes);
            }

            // Read data into memory space
            hsize_t memDims[] = {imageInfo.width};
            DataSpace memspace(1, memDims);
            auto sliceDataSpace = dataSets["main"].getSpace();
            sliceDataSpace.selectHyperslab(H5S_SELECT_SET, count.data(), start.data());
            dataSets["main"].read(profile.data(), PredType::NATIVE_FLOAT, memspace, sliceDataSpace);

            return profile;
        }
        catch (...) {
            log("Invalid profile request in file {}", imageInfo.filename);
            return vector<float>();
        }
    }
}

// Calculates a Y Profile for a given X pixel coordinate and channel
vector<float> Session::getYProfile(int x, int channel, int stokes) {
    if (!file) {
        log("No file loaded or invalid session");
        return vector<float>();
    } else if (x < 0 || x >= imageInfo.width || channel < 0 || channel >= imageInfo.depth || stokes < 0 || stokes >= imageInfo.stokes) {
        log("Y profile out of range");
        return vector<float>();
    }

    vector<float> profile;
    profile.resize(imageInfo.height);
    if ((channel == currentChannel && stokes == currentStokes) || imageInfo.dimensions == 2) {

        for (auto i = 0; i < imageInfo.height; i++) {
            profile[i] = currentChannelCache[i * imageInfo.width + x];
        }
        return profile;
    } else {
        try {

            // Defines dimensions of hyperslab in 3D
            vector<hsize_t> count = {1, imageInfo.height, 1};
            vector<hsize_t> start = {channel, 0, x};

            // Append stokes parameter to hyperslab dimensions in 4D
            if (imageInfo.dimensions == 4) {
                count.insert(count.begin(), 1);
                start.insert(start.begin(), stokes);
            }

            // Read data into memory space
            hsize_t memDims[] = {imageInfo.height};
            DataSpace memspace(1, memDims);
            auto sliceDataSpace = dataSets["main"].getSpace();
            sliceDataSpace.selectHyperslab(H5S_SELECT_SET, count.data(), start.data());
            dataSets["main"].read(profile.data(), PredType::NATIVE_FLOAT, memspace, sliceDataSpace);

            return profile;
        }
        catch (...) {
            log("Invalid profile request in file {}", imageInfo.filename);
            return vector<float>();
        }
    }
}

// Calculates a Z Profile for a given X and Y pixel coordinate
vector<float> Session::getZProfile(int x, int y, int stokes) {
    if (!file) {
        log("No file loaded or invalid session");
        return vector<float>();
    } else if (x < 0 || x >= imageInfo.width || y < 0 || y >= imageInfo.height || stokes < 0 || stokes >= imageInfo.stokes) {
        log("Z profile out of range");
        return vector<float>();
    }

    // If the requested coordinates are the same as the previous one, just use the cached value instead of reading from disk
    if (cachedZProfile.size() == imageInfo.depth && cachedZProfileCoords.size() == 3 && cachedZProfileCoords[0] == x
        && cachedZProfileCoords[1] == y && cachedZProfileCoords[2] == stokes) {
        return cachedZProfile;
    }

    if (imageInfo.dimensions == 2) {
        return {currentChannelCache[y * imageInfo.width + x]};
    }

    cachedZProfile.resize(imageInfo.depth);

    try {
        // Check if swizzled dataset exists
        if (dataSets.count("swizzled")) {
            // Defines dimensions of hyperslab in 3D
            vector<hsize_t> count = {1, 1, imageInfo.depth};
            vector<hsize_t> start = {x, y, 0};

            // Append stokes parameter to hyperslab dimensions in 4D
            if (imageInfo.dimensions == 4) {
                count.insert(count.begin(), 1);
                start.insert(start.begin(), stokes);
            }

            // Read data into memory space
            hsize_t memDims[] = {imageInfo.depth};
            DataSpace memspace(1, memDims);
            auto sliceDataSpace = dataSets["swizzled"].getSpace();
            sliceDataSpace.selectHyperslab(H5S_SELECT_SET, count.data(), start.data());
            dataSets["swizzled"].read(cachedZProfile.data(), PredType::NATIVE_FLOAT, memspace, sliceDataSpace);
        } else {
            // Defines dimensions of hyperslab in 3D
            vector<hsize_t> count = {imageInfo.depth, 1, 1};
            vector<hsize_t> start = {0, y, x};

            // Append stokes parameter to hyperslab dimensions in 4D
            if (imageInfo.dimensions == 4) {
                count.insert(count.begin(), 1);
                start.insert(start.begin(), stokes);
            }

            // Read data into memory space
            hsize_t memDims[] = {imageInfo.depth};
            DataSpace memspace(1, memDims);
            auto sliceDataSpace = dataSets["main"].getSpace();
            sliceDataSpace.selectHyperslab(H5S_SELECT_SET, count.data(), start.data());
            dataSets["main"].read(cachedZProfile.data(), PredType::NATIVE_FLOAT, memspace, sliceDataSpace);
        }
        cachedZProfileCoords = {x, y, stokes};
        return cachedZProfile;
    }
    catch (...) {
        log("Invalid profile request in file {}", imageInfo.filename);
        return vector<float>();
    }
}

// Reads a region corresponding to the given region request. If the current channel is not the same as
// the channel specified in the request, the new channel is loaded
vector<float> Session::readRegion(const Requests::RegionReadRequest& regionReadRequest, bool meanFilter) {
    if (!file) {
        log("No file loaded");
        return vector<float>();
    }

    if (currentChannel != regionReadRequest.channel() || currentStokes != regionReadRequest.stokes()) {
        if (!loadChannel(regionReadRequest.channel(), regionReadRequest.stokes())) {
            log("Selected channel {} is invalid!", regionReadRequest.channel());
            return vector<float>();
        }
    }

    const int mip = regionReadRequest.mip();
    const int x = regionReadRequest.x();
    const int y = regionReadRequest.y();
    const int height = regionReadRequest.height();
    const int width = regionReadRequest.width();

    if (imageInfo.height < y + height || imageInfo.width < x + width) {
        log("Selected region ({}, {}) -> ({}, {} in channel {} is invalid!",
            x, y, x + width, y + height, regionReadRequest.channel());
        return vector<float>();
    }

    size_t numRowsRegion = height / mip;
    size_t rowLengthRegion = width / mip;
    vector<float> regionData;
    regionData.resize(numRowsRegion * rowLengthRegion);

    if (meanFilter) {
        // Perform down-sampling by calculating the mean for each MIPxMIP block
        for (auto j = 0; j < numRowsRegion; j++) {
            for (auto i = 0; i < rowLengthRegion; i++) {
                float pixelSum = 0;
                int pixelCount = 0;
                for (auto pixelX = 0; pixelX < mip; pixelX++) {
                    for (auto pixelY = 0; pixelY < mip; pixelY++) {
                        float pixVal = currentChannelCache[(y + j * mip + pixelY) * imageInfo.width + (x + i * mip + pixelX)];
                        if (!isnan(pixVal)) {
                            pixelCount++;
                            pixelSum += pixVal;
                        }
                    }
                }
                regionData[j * rowLengthRegion + i] = pixelCount ? pixelSum / pixelCount : NAN;
            }
        }
    } else {
        // Nearest neighbour filtering
        for (auto j = 0; j < numRowsRegion; j++) {
            for (auto i = 0; i < rowLengthRegion; i++) {
                regionData[j * rowLengthRegion + i] = currentChannelCache[(y + j * mip) * imageInfo.width + (x + i * mip)];
            }
        }
    }
    return regionData;
}

vector<RegionStats> Session::getRegionStats(int xMin, int xMax, int yMin, int yMax, int channelMin, int channelMax, int stokes, RegionShapeType shapeType) {
    auto tStart = chrono::high_resolution_clock::now();
    vector<RegionStats> allStats(channelMax - channelMin);
    auto mask = getShapeMask(xMin, xMax, yMin, yMax, shapeType);
    int N = ((yMax - yMin) * (xMax - xMin));
    auto& dataSet = dataSets["main"];
    vector<float> data(N);

    for (auto i = channelMin; i < channelMax; i++) {
        float sum = 0;
        float sumSquared = 0;
        float minVal = numeric_limits<float>::max();
        float maxVal = -numeric_limits<float>::max();
        int nanCount = 0;
        int validCount = 0;

        // Define dimensions of hyperslab in 2D
        vector<hsize_t> count = {yMax - yMin, xMax - xMin};
        vector<hsize_t> start = {yMin, xMin};

        // Append channel (and stokes in 4D) to hyperslab dims
        if (imageInfo.dimensions == 3) {
            count.insert(count.begin(), 1);
            start.insert(start.begin(), i);
        } else if (imageInfo.dimensions == 4) {
            count.insert(count.begin(), {1, 1});
            start.insert(start.begin(), {stokes, i});
        }

        // Read data into memory
        hsize_t memDims[] = {N};
        DataSpace memspace(1, memDims);
        auto sliceDataSpace = dataSet.getSpace();
        sliceDataSpace.selectHyperslab(H5S_SELECT_SET, count.data(), start.data());
        dataSet.read(data.data(), PredType::NATIVE_FLOAT, memspace, sliceDataSpace);

        // Process data
        for (auto j = 0; j < N; j++) {
            auto& v = data[j];
            bool valid = !isnan(v) && mask[j];
            sum += valid ? v : 0;
            sumSquared += valid ? v * v : 0;
            minVal = valid ? fmin(minVal, v) : minVal;
            maxVal = valid ? fmax(maxVal, v) : maxVal;
            nanCount += !valid;
            validCount += valid;;
        }

        RegionStats stats;
        stats.minVal = minVal;
        stats.maxVal = maxVal;
        stats.mean = sum / max(validCount, 1);
        stats.stdDev = sqrtf(sumSquared / (max(validCount, 1)) - stats.mean * stats.mean);
        stats.nanCount = nanCount;
        stats.validCount = validCount;

        if (!stats.validCount) {
            stats.mean = NAN;
            stats.stdDev = NAN;
            stats.minVal = NAN;
            stats.maxVal = NAN;
        }

        allStats[i] = stats;
    }
    auto tEnd = chrono::high_resolution_clock::now();
    auto dtRegion = chrono::duration_cast<chrono::microseconds>(tEnd - tStart).count();
    log("{}x{} region stats for {} channels calculated in {:.1f} ms at {:.2f} ms/channel",
        xMax - xMin,
        yMax - yMin,
        allStats.size(),
        dtRegion * 1e-3,
        dtRegion * 1e-3 / allStats.size());
    return allStats;

}

vector<RegionStats> Session::getRegionStatsSwizzled(int xMin, int xMax, int yMin, int yMax, int channelMin, int channelMax, int stokes, RegionShapeType shapeType) {
    auto tStart = chrono::high_resolution_clock::now();
    vector<RegionStats> allStats(channelMax - channelMin);
    auto mask = getShapeMask(xMin, xMax, yMin, yMax, shapeType);
    auto numZ = channelMax - channelMin;
    auto numY = yMax - yMin;
    auto numX = xMax - xMin;
    auto N = numY * numZ;
    vector<float> data(N);
    auto& dataSetSwizzled = dataSets["swizzled"];

    for (auto x = 0; x < numX; x++) {
        // Define dimensions of hyperslab in 2D
        vector<hsize_t> count = {1, numY, channelMax - channelMin};
        vector<hsize_t> start = {x + xMin, yMin, channelMin};

        // Append stokes to hyperslab dims
        if (imageInfo.dimensions == 4) {
            count.insert(count.begin(), 1);
            start.insert(start.begin(), stokes);
        }

        // Read data into memory
        hsize_t memDims[] = {N};
        DataSpace memspace(1, memDims);
        auto sliceDataSpace = dataSetSwizzled.getSpace();
        sliceDataSpace.selectHyperslab(H5S_SELECT_SET, count.data(), start.data());
        dataSetSwizzled.read(data.data(), PredType::NATIVE_FLOAT, memspace, sliceDataSpace);

        for (auto y = 0; y < numY; y++) {
            // skip all Z values for masked pixels
            if (!mask[y * numX + x]) {
                continue;
            }
            for (auto z = 0; z < numZ; z++) {
                auto& stats = allStats[z];
                auto& v = data[y * numZ + z];
                stats.mean += isnan(v) ? 0 : v;;
                stats.stdDev += isnan(v) ? 0 : v * v;
                stats.minVal = fmin(stats.minVal, v);
                stats.maxVal = fmax(stats.maxVal, v);
                stats.nanCount += isnan(v);
                stats.validCount += !isnan(v);
            }
        }
    }

    for (auto z = 0; z < numZ; z++) {
        auto& stats = allStats[z];
        stats.mean /= max(stats.validCount, 1);
        stats.stdDev = sqrtf(stats.stdDev / max(stats.validCount, 1) - stats.mean * stats.mean);

        if (!stats.validCount) {
            stats.mean = NAN;
            stats.stdDev = NAN;
            stats.minVal = NAN;
            stats.maxVal = NAN;
        }
    }

    auto tEnd = chrono::high_resolution_clock::now();
    auto dtRegion = chrono::duration_cast<chrono::microseconds>(tEnd - tStart).count();
    log("{}x{} region stats for {} channels calculated in {:.1f} ms at {:.2f} ms/channel using swizzled dataset",
        xMax - xMin,
        yMax - yMin,
        allStats.size(),
        dtRegion * 1e-3,
        dtRegion * 1e-3 / allStats.size());
    return allStats;
}

std::vector<bool> Session::getShapeMask(int xMin, int xMax, int yMin, int yMax, RegionShapeType shapeType) {
    auto numY = abs(yMax - yMin);
    auto numX = abs(xMax - xMin);
    if (shapeType == RegionShapeType::RegionStatsRequest_ShapeType_RECTANGLE) {
        vector<bool> mask(numX * numY, true);
        return mask;
    } else {
        vector<bool> mask(numX * numY);
        float xC = (xMax + xMin) / 2.0f;
        float yC = (yMax + yMin) / 2.0f;
        float xR = numX / 2.0f;
        float yR = numY / 2.0f;
        for (auto y = yMin; y < yMax; y++) {
            for (auto x = xMin; x < xMax; x++) {
                float testVal = ((x - xC) * (x - xC)) / (xR * xR) + ((y - yC) * (y - yC)) / (yR * yR);
                mask[(y - yMin) * numX + (x - xMin)] = testVal <= 1;
            }
        }
        return mask;
    }
}

// Event response to region read request
void Session::onRegionReadRequest(const Requests::RegionReadRequest& regionReadRequest, u_int64_t requestId) {
    // Mutex used to prevent overlapping requests for a single client
    eventMutex.lock();

    // Valid compression precision range: (4-31)
    bool compressed = regionReadRequest.compression() >= 4 && regionReadRequest.compression() < 32;
    vector<float> regionData = readRegion(regionReadRequest, true);

    if (regionData.size()) {
        auto numValues = regionData.size();
        auto rowLength = regionReadRequest.width() / regionReadRequest.mip();
        auto numRows = regionReadRequest.height() / regionReadRequest.mip();

        regionReadResponse.set_success(true);
        regionReadResponse.set_compression(regionReadRequest.compression());
        regionReadResponse.set_x(regionReadRequest.x());
        regionReadResponse.set_y(regionReadRequest.y());
        regionReadResponse.set_width(rowLength);
        regionReadResponse.set_height(numRows);
        regionReadResponse.set_mip(regionReadRequest.mip());
        regionReadResponse.set_channel(regionReadRequest.channel());
        regionReadResponse.set_stokes(regionReadRequest.stokes());
        regionReadResponse.set_num_values(numValues);

        if (imageInfo.channelStats[currentStokes][currentChannel].nanCount != imageInfo.width * imageInfo.height) {
            auto& channelStats = imageInfo.channelStats[currentStokes][currentChannel];
            auto stats = regionReadResponse.mutable_stats();
            stats->set_mean(channelStats.mean);
            stats->set_min_val(channelStats.minVal);
            stats->set_max_val(channelStats.maxVal);
            stats->set_nan_counts(channelStats.nanCount);
            stats->clear_percentiles();
            auto percentiles = stats->mutable_percentiles();
            for (auto& v: channelStats.percentileRanks) {
                percentiles->add_ranks(v);
            }
            for (auto& v: channelStats.percentiles) {
                percentiles->add_values(v);
            }

            if (channelStats.histogramBins.size() && !isnan(channelStats.minVal) && !isnan(channelStats.maxVal)) {
                float binWidth = (channelStats.maxVal - channelStats.minVal) / channelStats.histogramBins.size();
                float firstBinCenter = channelStats.minVal + binWidth / 2.0;
                auto hist = stats->mutable_hist();
                hist->set_first_bin_center(firstBinCenter);
                hist->set_n(channelStats.histogramBins.size());
                hist->set_bin_width(binWidth);
                hist->set_bins(channelStats.histogramBins.data(), channelStats.histogramBins.size() * sizeof(int));
            } else {
                stats->clear_hist();
            }
        } else {
            regionReadResponse.clear_stats();
        }

        if (compressed) {
            int numSubsets = std::min(regionReadRequest.num_subsets(), MAX_SUBSETS);
            regionReadResponse.set_num_subsets(numSubsets);
            regionReadResponse.clear_image_data();
            regionReadResponse.clear_nan_encodings();

            vector<size_t> compressedSizes(numSubsets);
            vector<vector<int32_t>> nanEncodings(numSubsets);
            vector<future<size_t>> futureSizes;

            auto tStartCompress = chrono::high_resolution_clock::now();
            for (auto i = 0; i < numSubsets; i++) {
                auto& compressionBuffer = compressionBuffers[i];
                int compression = regionReadRequest.compression();
                futureSizes.push_back(threadPool.push([&nanEncodings, &regionData, &compressionBuffer, numRows, numSubsets, rowLength, i, compression](int) {
                    int subsetRowStart = i * (numRows / numSubsets);
                    int subsetRowEnd = (i + 1) * (numRows / numSubsets);
                    if (i == numSubsets - 1) {
                        subsetRowEnd = numRows;
                    }
                    int subsetElementStart = subsetRowStart * rowLength;
                    int subsetElementEnd = subsetRowEnd * rowLength;

                    size_t compressedSize;
                    nanEncodings[i] = getNanEncodings(regionData, subsetElementStart, subsetElementEnd - subsetElementStart);
                    compress(regionData, subsetElementStart, compressionBuffer, compressedSize, rowLength, subsetRowEnd - subsetRowStart, compression);
                    return compressedSize;
                }));
            }

            for (auto i = 0; i < numSubsets; i++) {
                compressedSizes[i] = futureSizes[i].get();
            }

            auto tEndCompress = chrono::high_resolution_clock::now();
            auto dtCompress = chrono::duration_cast<chrono::microseconds>(tEndCompress - tStartCompress).count();

            // Don't include all-NaN calculations in the average speed calculation
            if (regionReadResponse.stats().nan_counts() != regionReadResponse.num_values()) {
                rateSum += (float) (numRows * rowLength) / dtCompress;
                rateCount++;
            }

            if (verboseLogging) {
                log("Image data of size {:.1f} kB compressed to {:.1f} kB in {} μs at {:.2f} Mpix/s using {} threads (Average {:.2f} Mpix/s)",
                    numRows * rowLength * sizeof(float) / 1e3,
                    accumulate(compressedSizes.begin(), compressedSizes.end(), 0) / 1e3,
                    dtCompress, (float) (numRows * rowLength) / dtCompress, numSubsets, rateSum / max(rateCount, 1));
            }

            for (auto i = 0; i < numSubsets; i++) {
                if (regionReadResponse.image_data_size() < i + 1) {
                    regionReadResponse.add_image_data(compressionBuffers[i].data(), compressedSizes[i]);
                } else {
                    regionReadResponse.set_image_data(i, compressionBuffers[i].data(), compressedSizes[i]);
                }

                if (regionReadResponse.nan_encodings_size() < i + 1) {
                    regionReadResponse.add_nan_encodings((char*) nanEncodings[i].data(), nanEncodings[i].size() * sizeof(int));
                } else {
                    regionReadResponse.set_nan_encodings(i, (char*) nanEncodings[i].data(), nanEncodings[i].size() * sizeof(int));
                }
            }
        } else {
            regionReadResponse.set_num_subsets(1);
            regionReadResponse.clear_nan_encodings();
            regionReadResponse.clear_image_data();
            auto tStart = chrono::high_resolution_clock::now();
            if (regionReadResponse.image_data_size() < 1) {
                regionReadResponse.add_image_data(regionData.data(), numRows * rowLength * sizeof(float));
            } else {
                regionReadResponse.set_image_data(0, regionData.data(), numRows * rowLength * sizeof(float));
            }
            auto tEnd = chrono::high_resolution_clock::now();
            auto dtSetImageData = chrono::duration_cast<chrono::microseconds>(tEnd - tStart).count();
            if (verboseLogging) {
                fmt::print("Image data of size {:.1f} kB copied to protobuf in {} μs\n",
                           numRows * rowLength * sizeof(float) / 1e3,
                           dtSetImageData);
            }
        }

    } else {
        log("ReadRegion request is out of bounds");
        regionReadResponse.set_success(false);
    }
    eventMutex.unlock();
    sendEvent("region_read", requestId, regionReadResponse);
}

// Event response to file load request
void Session::onFileLoad(const Requests::FileLoadRequest& fileLoadRequest, u_int64_t requestId) {
    eventMutex.lock();
    Responses::FileLoadResponse fileLoadResponse;
    if (loadFile(fileLoadRequest.filename())) {
        log("File {} loaded successfully", fileLoadRequest.filename());

        fileLoadResponse.set_success(true);
        fileLoadResponse.set_filename(fileLoadRequest.filename());
        fileLoadResponse.set_image_width(imageInfo.width);
        fileLoadResponse.set_image_height(imageInfo.height);
        fileLoadResponse.set_image_depth(imageInfo.depth);
        fileLoadResponse.set_image_stokes(imageInfo.stokes);

    } else {
        log("Error loading file {}", fileLoadRequest.filename());
        fileLoadResponse.set_success(false);
    }
    eventMutex.unlock();
    sendEvent("fileload", requestId, fileLoadResponse);
}

// Event response to profile request
void Session::onProfileRequest(const Requests::ProfileRequest& request, u_int64_t requestId) {
    eventMutex.lock();
    Responses::ProfileResponse response;
    response.set_x(request.x());
    response.set_y(request.y());
    response.set_channel(request.channel());
    response.set_stokes(request.stokes());

    bool validX = request.x() >= 0 && request.x() < imageInfo.width;
    bool validY = request.y() >= 0 && request.y() < imageInfo.height;
    bool validZ = request.channel() >= 0 && request.channel() < imageInfo.depth;
    bool validW = request.stokes() >= 0 && request.stokes() < imageInfo.stokes;

    if (validX && validY && validZ && validW) {
        bool requestSuccess = true;
        if (request.request_x()) {
            vector<float> profileX = getXProfile(request.y(), request.channel(), request.stokes());
            if (profileX.size()) {
                google::protobuf::RepeatedField<float> data(profileX.begin(), profileX.end());
                response.mutable_x_profile()->Swap(&data);
            } else {
                requestSuccess = false;
            }
        }
        // skip further profile calculations if the request has already failed
        if (requestSuccess && request.request_y()) {
            vector<float> profileY = getYProfile(request.x(), request.channel(), request.stokes());
            if (profileY.size()) {
                google::protobuf::RepeatedField<float> data(profileY.begin(), profileY.end());
                response.mutable_y_profile()->Swap(&data);
            } else {
                requestSuccess = false;
            }
        }
        if (requestSuccess && request.request_z()) {
            vector<float> profileZ = getZProfile(request.x(), request.y(), request.stokes());
            if (profileZ.size()) {
                google::protobuf::RepeatedField<float> data(profileZ.begin(), profileZ.end());
                response.mutable_z_profile()->Swap(&data);
            } else {
                requestSuccess = false;
            }
        }
        response.set_success(requestSuccess);
    } else {
        response.set_success(false);
    }
    eventMutex.unlock();
    sendEvent("profile", requestId, response);
}

// Event response to region stats request
void Session::onRegionStatsRequest(const Requests::RegionStatsRequest& request, u_int64_t requestId) {
    Responses::RegionStatsResponse response;
    response.set_x(request.x());
    response.set_y(request.y());
    response.set_stokes(request.stokes());
    response.set_width(request.width());
    response.set_height(request.height());

    bool validX = request.x() >= 0 && request.x() + request.width() < imageInfo.width;
    bool validY = request.y() >= 0 && request.y() + request.height() < imageInfo.height;
    bool validW = request.stokes() >= 0 && request.stokes() < imageInfo.stokes;

    if (validX && validY && validW) {
        vector<RegionStats> allStats;
        if (imageInfo.dimensions == 2) {
            allStats = {getRegionStats(request.x(), request.x() + request.width(), request.y(), request.y() + request.height(), 0, 1, 0, request.shape_type())};
        } else if (dataSets.count("swizzled")) {
            allStats = getRegionStatsSwizzled(request.x(), request.x() + request.width(), request.y(), request.y() + request.height(), 0, imageInfo.depth, request.stokes(), request.shape_type());
        } else {
            allStats = getRegionStats(request.x(), request.x() + request.width(), request.y(), request.y() + request.height(), 0, imageInfo.depth, request.stokes(), request.shape_type());
        }

        for (auto& stats: allStats) {
            response.add_min_vals(stats.minVal);
            response.add_max_vals(stats.maxVal);
            response.add_means(stats.mean);
            response.add_std_devs(stats.stdDev);
            response.add_nan_counts(stats.nanCount);
        }
        response.set_success(true);
    } else {
        response.set_success(false);
    }
    sendEvent("region_stats", requestId, response);
}

// CARTA ICD implementation
void Session::onRegisterViewer(const CARTA::RegisterViewer& message, uint64_t requestId) {
    apiKey = message.api_key();
    CARTA::RegisterViewerAck ackMessage;
    ackMessage.set_success(true);
    ackMessage.set_session_id(boost::uuids::to_string(uuid));
    sendEvent("REGISTER_VIEWER_ACK", requestId, ackMessage);
}

void Session::onFileListRequest(const CARTA::FileListRequest& request, uint64_t requestId) {
    string folder = request.directory();
    if (folder.length() > 1 && folder[0] == '/') {
        folder = folder.substr(1);
    }
    CARTA::FileListResponse response = getFileList(folder);
    sendEvent("FILE_LIST_RESPONSE", requestId, response);
}

void Session::onFileInfoRequest(const CARTA::FileInfoRequest& request, uint64_t requestId) {
    CARTA::FileInfoResponse response;
    auto fileInfo = response.mutable_file_info();
    auto fileInfoExtended = response.mutable_file_info_extended();
    string message;
    bool success = fillExtendedFileInfo(fileInfoExtended, fileInfo, request.directory(), request.file(), request.hdu(), message);
    response.set_success(success);
    response.set_message(message);
    sendEvent("FILE_INFO_RESPONSE", requestId, response);
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

// Helper functions for logging

void Session::log(const string& logMessage) {
    // Shorten uuids a bit for brevity
    auto uuidString = boost::uuids::to_string(uuid);
    auto lastHash = uuidString.find_last_of('-');
    if (lastHash != string::npos) {
        uuidString = uuidString.substr(lastHash + 1);
    }
    time_t time = chrono::system_clock::to_time_t(chrono::system_clock::now());
    string timeString = ctime(&time);
    timeString = timeString.substr(0, timeString.length() - 1);

    fmt::print("Session {} [{}] ({}): {}\n", uuidString, socket->getAddress().address, timeString, logMessage);
}

template<typename... Args>
void Session::log(const char* templateString, Args... args) {
    log(fmt::format(templateString, args...));
}

template<typename... Args>
void Session::log(const std::string& templateString, Args... args) {
    log(fmt::format(templateString, args...));
}
