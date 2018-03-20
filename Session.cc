#include "Session.h"
#include <fstream>
#include <boost/filesystem.hpp>
#include <proto/fileLoadResponse.pb.h>
#include <proto/connectionResponse.pb.h>
#include <proto/profileResponse.pb.h>
#include <proto/regionStatsResponse.pb.h>

using namespace HighFive;
using namespace std;
using namespace uWS;
namespace fs = boost::filesystem;

// Default constructor. Associates a websocket with a UUID and sets the base folder for all files
Session::Session(WebSocket<SERVER>* ws, boost::uuids::uuid uuid, string folder, ctpl::thread_pool& serverThreadPool, bool verbose)
    : uuid(uuid),
      currentChannel(-1),
      currentStokes(-1),
      file(nullptr),
      baseFolder(folder),
      verboseLogging(verbose),
      threadPool(serverThreadPool),
      rateSum(0),
      rateCount(0),
      socket(ws) {

    eventMutex.lock();
    auto tStart = chrono::high_resolution_clock::now();
    availableFileList = getAvailableFiles(baseFolder);
    auto tEnd = chrono::high_resolution_clock::now();
    auto dtFileSearch = chrono::duration_cast<chrono::milliseconds>(tEnd - tStart).count();
    fmt::print("Found {} HDF5 files in {} ms\n", availableFileList.size(), dtFileSearch);

    Responses::ConnectionResponse connectionResponse;
    connectionResponse.set_success("true");
    for (auto& v: availableFileList) {
        connectionResponse.add_available_files(v);
    }
    eventMutex.unlock();
    sendEvent("connect", connectionResponse);
}

Session::~Session() {

}

vector<string> Session::getAvailableFiles(const string& folder, string prefix) {
    fs::path folderPath(folder);
    vector<string> files;
    try {
        if (fs::exists(folderPath) && fs::is_directory(folderPath)) {
            for (auto& directoryEntry : fs::directory_iterator(folderPath)) {
                fs::path filePath(directoryEntry);
                if (fs::is_regular_file(filePath) && fs::file_size(directoryEntry) > 8) {
                    uint64_t sig;
                    string filename = directoryEntry.path().string();
                    ifstream file(filename, ios::in | ios::binary);
                    if (file.is_open()) {
                        file.read((char*) &sig, 8);
                        if (sig == 0xa1a0a0d46444889) {
                            files.push_back(prefix + directoryEntry.path().filename().string());
                        }
                    }
                    file.close();
                } else if (fs::is_directory(filePath)) {
                    string newPrefix = filePath.string() + "/";
                    // Strip out the leading "./" in subdirectories
                    if (newPrefix.length() > 2 && newPrefix.substr(0, 2) == "./") {
                        newPrefix = prefix.substr(2);
                    }

                    // Strip out base folder path as well (boost < 1.60 doesn't have relative path functionality)
                    if (newPrefix.length() > folderPath.string().length() && newPrefix.substr(0, folderPath.string().length()) == folderPath.string()) {
                        newPrefix = newPrefix.substr(folderPath.string().length());
                        if (newPrefix.length() > 0 && newPrefix[0] == '/') {
                            newPrefix = newPrefix.substr(1);
                        }
                    }

                    auto dir_files = getAvailableFiles(filePath.string(), prefix + newPrefix);
                    files.insert(files.end(), dir_files.begin(), dir_files.end());
                }
            }

        }
    }
    catch (const fs::filesystem_error& ex) {
        fmt::print("Error: {}\n", ex.what());
    }
    return files;
}

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

bool Session::loadStats() {
    if (!file || !file->isValid()) {
        log("No file loaded");
        return false;
    }

    imageInfo.channelStats.resize(imageInfo.stokes);
    for (auto i = 0; i < imageInfo.stokes; i++) {
        imageInfo.channelStats[i].resize(imageInfo.depth);
    }

    if (file->exist("0/Statistics/XY")) {
        auto group = file->getGroup("0");
        auto statsGroup = file->getGroup("0/Statistics/XY");
        if (statsGroup.isValid() && statsGroup.exist("MAX")) {
            auto dataSet = statsGroup.getDataSet("MAX");
            auto dims = dataSet.getSpace().getDimensions();

            // 2D cubes
            if (imageInfo.dimensions == 2 && dims.size() == 0) {
                dataSet.read(&imageInfo.channelStats[0][0].maxVal);
            } // 3D cubes
            else if (imageInfo.dimensions == 3 && dims.size() == 1 && dims[0] == imageInfo.depth) {
                vector<float> data;
                dataSet.read(data);
                for (auto i = 0; i < imageInfo.depth; i++) {
                    imageInfo.channelStats[0][i].maxVal = data[i];
                }
            } // 4D cubes
            else if (imageInfo.dimensions == 4 && dims.size() == 2 && dims[0] == imageInfo.stokes && dims[1] == imageInfo.depth) {
                vector<vector<float>> data;
                dataSet.read(data);
                for (auto i = 0; i < imageInfo.stokes; i++) {
                    for (auto j = 0; j < imageInfo.depth; j++) {
                        imageInfo.channelStats[i][j].maxVal = data[i][j];
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

        if (statsGroup.isValid() && statsGroup.exist("MIN")) {
            auto dataSet = statsGroup.getDataSet("MIN");
            auto dims = dataSet.getSpace().getDimensions();

            // 2D cubes
            if (imageInfo.dimensions == 2 && dims.size() == 0) {
                dataSet.read(&imageInfo.channelStats[0][0].minVal);
            } // 3D cubes
            else if (imageInfo.dimensions == 3 && dims.size() == 1 && dims[0] == imageInfo.depth) {
                vector<float> data;
                dataSet.read(data);
                for (auto i = 0; i < imageInfo.depth; i++) {
                    imageInfo.channelStats[0][i].minVal = data[i];
                }
            } // 4D cubes
            else if (imageInfo.dimensions == 4 && dims.size() == 2 && dims[0] == imageInfo.stokes && dims[1] == imageInfo.depth) {
                vector<vector<float>> data;
                dataSet.read(data);
                for (auto i = 0; i < imageInfo.stokes; i++) {
                    for (auto j = 0; j < imageInfo.depth; j++) {
                        imageInfo.channelStats[i][j].minVal = data[i][j];
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

        if (statsGroup.isValid() && statsGroup.exist("MEAN")) {
            auto dataSet = statsGroup.getDataSet("MEAN");
            auto dims = dataSet.getSpace().getDimensions();

            // 2D cubes
            if (imageInfo.dimensions == 2 && dims.size() == 0) {
                dataSet.read(&imageInfo.channelStats[0][0].mean);
            } // 3D cubes
            else if (imageInfo.dimensions == 3 && dims.size() == 1 && dims[0] == imageInfo.depth) {
                vector<float> data;
                dataSet.read(data);
                for (auto i = 0; i < imageInfo.depth; i++) {
                    imageInfo.channelStats[0][i].mean = data[i];
                }
            } // 4D cubes
            else if (imageInfo.dimensions == 4 && dims.size() == 2 && dims[0] == imageInfo.stokes && dims[1] == imageInfo.depth) {
                vector<vector<float>> data;
                dataSet.read(data);
                for (auto i = 0; i < imageInfo.stokes; i++) {
                    for (auto j = 0; j < imageInfo.depth; j++) {
                        imageInfo.channelStats[i][j].mean = data[i][j];
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

        if (statsGroup.isValid() && statsGroup.exist("NAN_COUNT")) {
            auto dataSet = statsGroup.getDataSet("NAN_COUNT");
            auto dims = dataSet.getSpace().getDimensions();

            // 2D cubes
            if (imageInfo.dimensions == 2 && dims.size() == 0) {
                dataSet.read(&imageInfo.channelStats[0][0].nanCount);
            } // 3D cubes
            else if (imageInfo.dimensions == 3 && dims.size() == 1 && dims[0] == imageInfo.depth) {
                vector<int64_t> data;
                dataSet.read(data);
                for (auto i = 0; i < imageInfo.depth; i++) {
                    imageInfo.channelStats[0][i].nanCount = data[i];
                }
            } // 4D cubes
            else if (imageInfo.dimensions == 4 && dims.size() == 2 && dims[0] == imageInfo.stokes && dims[1] == imageInfo.depth) {
                vector<vector<int64_t>> data;
                dataSet.read(data);
                for (auto i = 0; i < imageInfo.stokes; i++) {
                    for (auto j = 0; j < imageInfo.depth; j++) {
                        imageInfo.channelStats[i][j].nanCount = data[i][j];
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

        if (statsGroup.exist("HISTOGRAM")) {
            auto dataSet = statsGroup.getDataSet("HISTOGRAM");
            auto dims = dataSet.getSpace().getDimensions();

            // 2D cubes
            if (imageInfo.dimensions == 2) {
                vector<int> data;
                dataSet.read(data);
                imageInfo.channelStats[0][0].histogramBins = data;
            } // 3D cubes
            else if (imageInfo.dimensions == 3 && dims.size() == 2 && dims[0] == imageInfo.depth) {
                vector<vector<int>> data;
                dataSet.read(data);
                for (auto i = 0; i < imageInfo.depth; i++) {
                    imageInfo.channelStats[0][i].histogramBins = data[i];
                }
            } // 4D cubes
            else if (imageInfo.dimensions == 4 && dims.size() == 3 && dims[0] == imageInfo.stokes && dims[1] == imageInfo.depth) {
                Matrix3I data;
                dataSet.read(data);
                for (auto i = 0; i < imageInfo.stokes; i++) {
                    for (auto j = 0; j < imageInfo.depth; j++) {
                        auto& stats = imageInfo.channelStats[i][j];
                        auto L = data.shape()[2];
                        stats.histogramBins.resize(L);
                        for (auto k = 0; k < L; k++) {
                            stats.histogramBins[k] = data[i][j][k];
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
        if (statsGroup.exist("PERCENTILES") && group.exist("PERCENTILE_RANKS")) {
            auto dataSetPercentiles = statsGroup.getDataSet("PERCENTILES");
            auto dataSetPercentilesRank = group.getDataSet("PERCENTILE_RANKS");

            auto dims = dataSetPercentiles.getSpace().getDimensions();
            auto dimsRanks = dataSetPercentilesRank.getSpace().getDimensions();
            vector<float> ranks;
            dataSetPercentilesRank.read(ranks);

            if (imageInfo.dimensions == 2 && dims.size() == 1 && dims[0] == dimsRanks[0]) {
                vector<float> vals;
                dataSetPercentiles.read(vals);
                imageInfo.channelStats[0][0].percentiles = vals;
                imageInfo.channelStats[0][0].percentileRanks = ranks;
            }
                // 3D cubes
            else if (imageInfo.dimensions == 3 && dims.size() == 2 && dims[0] == imageInfo.depth && dims[1] == dimsRanks[0]) {
                vector<vector<float>> vals;
                dataSetPercentiles.read(vals);

                for (auto i = 0; i < imageInfo.depth; i++) {
                    imageInfo.channelStats[0][i].percentiles = vals[i];
                    imageInfo.channelStats[0][i].percentileRanks = ranks;
                }
            }
                // 4D cubes
            else if (imageInfo.dimensions == 4 && dims.size() == 3 && dims[0] == imageInfo.stokes && dims[1] == imageInfo.depth && dims[2] == dimsRanks[0]) {
                Matrix3F vals;
                dataSetPercentiles.read(vals);

                for (auto i = 0; i < imageInfo.stokes; i++) {
                    for (auto j = 0; j < imageInfo.depth; j++) {
                        auto L = vals.shape()[2];
                        auto& stats = imageInfo.channelStats[i][j];
                        stats.percentiles.resize(L);
                        for (auto k = 0; k < L; k++) {
                            stats.percentiles[k] = vals[i][j][k];
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

bool Session::loadChannel(int channel, int stokes) {

    if (!file || !file->isValid()) {

        log("No file loaded");
        return false;
    } else if (channel < 0 || channel >= imageInfo.depth || stokes < 0 || stokes >= imageInfo.stokes) {
        log("Channel {} (stokes {}) is invalid in file {}", channel, stokes, imageInfo.filename);
        return false;
    }

    if (imageInfo.dimensions == 2) {
        dataSets.at("main").read(currentChannelCache2D);
        currentChannelCache = currentChannelCache2D.data();
    } else if (imageInfo.dimensions == 3) {
        dataSets.at("main").select({channel, 0, 0}, {1, imageInfo.height, imageInfo.width}).read(currentChannelCache3D);
        currentChannelCache = currentChannelCache3D.data();
    } else {
        dataSets.at("main").select({stokes, channel, 0, 0}, {1, 1, imageInfo.height, imageInfo.width}).read(currentChannelCache4D);
        currentChannelCache = currentChannelCache4D.data();
    }

    currentStokes = stokes;
    currentChannel = channel;
    updateHistogram();
    return true;
}

// Loads a file and the default channel.
bool Session::loadFile(const string& filename, int defaultChannel) {
    if (filename == imageInfo.filename) {
        return true;
    }

    if (find(availableFileList.begin(), availableFileList.end(), filename) == availableFileList.end()) {
        log("Problem loading file {}: File is not in available file list.", filename);
        return false;
    }

    try {
        file.reset(new File(fmt::format("{}/{}", baseFolder, filename), File::ReadOnly));
        vector<string> fileObjectList = file->listObjectNames();
        imageInfo.filename = filename;
        auto group = file->getGroup("0");

        DataSet dataSet = group.getDataSet("DATA");

        auto dims = dataSet.getSpace().getDimensions();
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
        dataSets.emplace("main", dataSet);

        if (group.exist("Statistics") && group.exist("Statistics/Z") && group.exist("Statistics/Z/MEAN")) {
            dataSets.emplace("average", group.getDataSet("Statistics/Z/MEAN"));
        }

        loadStats();

        if (group.exist("SwizzledData")) {
            if (imageInfo.dimensions == 3 && group.exist("SwizzledData/ZYX")) {
                DataSet dataSetSwizzled = group.getDataSet("SwizzledData/ZYX");
                auto swizzledDims = dataSetSwizzled.getSpace().getDimensions();
                if (swizzledDims.size() != 3 || swizzledDims[0] != dims[2]) {
                    log("Invalid swizzled data set in file {}, ignoring.", filename);
                } else {
                    log("Found valid swizzled data set in file {}.", filename);
                    dataSets.emplace("swizzled", dataSetSwizzled);
                }
            } else if (imageInfo.dimensions == 4 && group.exist("SwizzledData/ZYXW")) {
                DataSet dataSetSwizzled = group.getDataSet("SwizzledData/ZYXW");
                auto swizzledDims = dataSetSwizzled.getSpace().getDimensions();
                if (swizzledDims.size() != 4 || swizzledDims[1] != dims[3]) {
                    log("Invalid swizzled data set in file {}, ignoring.", filename);
                } else {
                    log("Found valid swizzled data set in file {}.", filename);
                    dataSets.emplace("swizzled", dataSetSwizzled);
                }
            } else {
                log("File {} missing optional swizzled data set, using fallback calculation.", filename);
            }
        } else {
            log("File {} missing optional swizzled data set, using fallback calculation.", filename);
        }
        return loadChannel(defaultChannel, 0);
    }
    catch (HighFive::Exception& err) {
        log("Problem loading file {}", filename);
        return false;
    }
}

// Calculates an X Profile for a given Y pixel coordinate and channel
vector<float> Session::getXProfile(int y, int channel, int stokes) {
    if (!file || !file->isValid()) {
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
            if (imageInfo.dimensions == 3) {
                Matrix3F yP;
                dataSets.at("main").select({channel, y, 0}, {1, 1, imageInfo.width}).read(yP);
                memcpy(profile.data(), yP.data(), imageInfo.width * sizeof(float));
            } else {
                Matrix4F yP;
                dataSets.at("main").select({stokes, channel, y, 0}, {1, 1, 1, imageInfo.width}).read(yP);
                memcpy(profile.data(), yP.data(), imageInfo.width * sizeof(float));
            }
            return profile;
        }
        catch (HighFive::Exception& err) {
            log("Invalid profile request in file {}", imageInfo.filename);
            return vector<float>();
        }
    }
}

// Calculates a Y Profile for a given X pixel coordinate and channel
vector<float> Session::getYProfile(int x, int channel, int stokes) {
    if (!file || !file->isValid()) {
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
            if (imageInfo.dimensions == 3) {
                Matrix3F yP;
                dataSets.at("main").select({channel, 0, x}, {1, imageInfo.height, 1}).read(yP);
                memcpy(profile.data(), yP.data(), imageInfo.height * sizeof(float));
            } else {
                Matrix4F yP;
                dataSets.at("main").select({stokes, channel, 0, x}, {1, 1, imageInfo.height, 1}).read(yP);
                memcpy(profile.data(), yP.data(), imageInfo.height * sizeof(float));
            }
            return profile;
        }
        catch (HighFive::Exception& err) {
            log("Invalid profile request in file {}", imageInfo.filename);
            return vector<float>();
        }
    }
}

// Calculates a Z Profile for a given X and Y pixel coordinate
vector<float> Session::getZProfile(int x, int y, int stokes) {
    if (!file || !file->isValid()) {
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

    try {
        // Check if swizzled dataset exists
        if (dataSets.count("swizzled")) {
            // Even when reading a single slice, since we're selecting a 3D space, we need to read into a 3D data structure
            // and then copy to a 1D vector. This is a bug in HighFive that only occurs if the last dimension is zero
            if (imageInfo.dimensions == 3) {
                Matrix3F zP;
                dataSets.at("swizzled").select({x, y, 0}, {1, 1, imageInfo.depth}).read(zP);
                cachedZProfile.resize(imageInfo.depth);
                memcpy(cachedZProfile.data(), zP.data(), imageInfo.depth * sizeof(float));
            } else {
                Matrix4F zP;
                dataSets.at("swizzled").select({stokes, x, y, 0}, {1, 1, 1, imageInfo.depth}).read(zP);
                cachedZProfile.resize(imageInfo.depth);
                memcpy(cachedZProfile.data(), zP.data(), imageInfo.depth * sizeof(float));
            }
        } else {
            if (imageInfo.dimensions == 3) {
                dataSets.at("main").select({0, y, x}, {imageInfo.depth, 1, 1}).read(cachedZProfile);
            } else {
                Matrix4F zP;
                dataSets.at("main").select({stokes, 0, y, x}, {1, imageInfo.depth, 1, 1}).read(zP);
                cachedZProfile.resize(imageInfo.depth);
                memcpy(cachedZProfile.data(), zP.data(), imageInfo.depth * sizeof(float));

            }
        }
        cachedZProfileCoords = {x, y, stokes};
        return cachedZProfile;
    }
    catch (HighFive::Exception& err) {
        log("Invalid profile request in file {}", imageInfo.filename);
        return vector<float>();
    }
}

// Reads a region corresponding to the given region request. If the current channel is not the same as
// the channel specified in the request, the new channel is loaded
vector<float> Session::readRegion(const Requests::RegionReadRequest& regionReadRequest, bool meanFilter) {
    if (!file || !file->isValid()) {
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
    Matrix2F processSlice2D;
    Matrix3F processSlice3D;
    Matrix4F processSlice4D;
    auto mask = getShapeMask(xMin, xMax, yMin, yMax, shapeType);
    for (auto i = channelMin; i < channelMax; i++) {
        float sum = 0;
        float sumSquared = 0;
        float minVal = numeric_limits<float>::max();
        float maxVal = -numeric_limits<float>::max();
        int nanCount = 0;
        int validCount = 0;
        int N = ((yMax - yMin) * (xMax - xMin));
        auto& dataSet = dataSets.at("main");
        float* data = nullptr;
        if (imageInfo.dimensions == 4) {
            dataSet.select({stokes, i, yMin, xMin}, {1, 1, yMax - yMin, xMax - xMin}).read(processSlice4D);
            data = processSlice4D.data();
        } else if (imageInfo.dimensions == 3) {
            dataSet.select({i, yMin, xMin}, {1, yMax - yMin, xMax - xMin}).read(processSlice3D);
            data = processSlice3D.data();
        } else {
            dataSet.select({yMin, xMin}, {yMax - yMin, xMax - xMin}).read(processSlice2D);
            data = processSlice2D.data();
        }
        for (auto j = 0; j < N; j++) {
            auto& v = data[j];
            if (!mask[N]) {
                continue;
            }
            sum += isnan(v) ? 0 : v;
            sumSquared += isnan(v) ? 0 : v * v;
            minVal = fmin(minVal, v);
            maxVal = fmax(maxVal, v);
            nanCount += isnan(v);
            validCount += !isnan(v);
        }
        RegionStats stats;
        stats.minVal = minVal;
        stats.maxVal = maxVal;
        stats.mean = sum / max(validCount, 1);
        stats.stdDev = sqrtf(sumSquared / (max(validCount, 1)) - stats.mean * stats.mean);
        stats.nanCount = nanCount;
        stats.validCount = validCount;
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
    Matrix3F processSlice3D;
    Matrix4F processSlice4D;
    auto mask = getShapeMask(xMin, xMax, yMin, yMax, shapeType);
    auto numZ = channelMax - channelMin;
    auto numY = yMax - yMin;
    auto numX = xMax - xMin;
    for (auto x = 0; x < numX; x++) {
        auto& dataSetSwizzled = dataSets.at("swizzled");
        float* data = nullptr;
        if (imageInfo.dimensions == 4) {
            dataSetSwizzled.select({stokes, x + xMin, yMin, channelMin}, {1, 1, numY, channelMax - channelMin}).read(processSlice4D);
            data = processSlice4D.data();
        } else {
            dataSetSwizzled.select({x + xMin, yMin, channelMin}, {1, numY, channelMax - channelMin}).read(processSlice3D);
            data = processSlice3D.data();
        }
        for (auto y = 0; y < numY; y++) {
            // skip masked values
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
    int N = ((yMax - yMin) * (xMax - xMin));
    for (auto z = 0; z < numZ; z++) {
        auto& stats = allStats[z];
        stats.mean /= max(stats.validCount, 1);
        stats.stdDev = sqrtf(stats.stdDev / max(stats.validCount, 1) - stats.mean * stats.mean);
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
void Session::onRegionReadRequest(const Requests::RegionReadRequest& regionReadRequest) {
    // Mutex used to prevent overlapping requests for a single client
    eventMutex.lock();

    // Valid compression precision range: (4-31)
    bool compressed = regionReadRequest.compression() >= 4 && regionReadRequest.compression() < 32;
    auto tStartRead = chrono::high_resolution_clock::now();
    vector<float> regionData = readRegion(regionReadRequest, false);
    auto tEndRead = chrono::high_resolution_clock::now();
    auto dtRead = chrono::duration_cast<chrono::microseconds>(tEndRead - tStartRead).count();

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
    sendEvent("region_read", regionReadResponse);
}

// Event response to file load request
void Session::onFileLoad(const Requests::FileLoadRequest& fileLoadRequest) {
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
    sendEvent("fileload", fileLoadResponse);
}

// Event response to profile request
void Session::onProfileRequest(const Requests::ProfileRequest& request) {
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
    sendEvent("profile", response);
}

// Event response to region stats request
void Session::onRegionStatsRequest(const Requests::RegionStatsRequest& request) {
    Responses::RegionStatsResponse response;
    response.set_x(request.x());
    response.set_y(request.y());
    response.set_stokes(request.stokes());
    response.set_width(request.width());
    response.set_height(request.height());

    bool validX = request.x() >= 0 && request.x() < imageInfo.width;
    bool validY = request.y() >= 0 && request.y() < imageInfo.height;
    bool validW = request.stokes() >= 0 && request.stokes() < imageInfo.stokes;

    if (validX && validY && validW) {
        vector<RegionStats> allStats;
        if (imageInfo.dimensions == 2) {
            allStats = {getRegionStats(request.x(), request.x() + request.width(), request.y(), request.y() + request.height(), 0, 1, 0, request.shape_type())};
        } else if (dataSets.count("swizzled")) {
            allStats = getRegionStatsSwizzled(request.x(), request.x() + request.width(), request.y(), request.y() + request.height(), 0, imageInfo.depth, request.stokes(), request.shape_type());
        } else {
            allStats =
                getRegionStats(request.x(), request.x() + request.width(), request.y(), request.y() + request.height(), 0, imageInfo.depth, request.stokes(), request.shape_type());
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

    sendEvent("region_stats", response);
}

// Sends an event to the client with a given event name (padded/concatenated to 32 characters) and a given protobuf message
void Session::sendEvent(string eventName, google::protobuf::MessageLite& message) {
    size_t eventNameLength = 32;
    int messageLength = message.ByteSize();
    size_t requiredSize = eventNameLength + messageLength;
    if (binaryPayloadCache.size() < requiredSize) {
        binaryPayloadCache.resize(requiredSize);
    }
    memset(binaryPayloadCache.data(), 0, eventNameLength);
    memcpy(binaryPayloadCache.data(), eventName.c_str(), min(eventName.length(), eventNameLength));
    message.SerializeToArray(binaryPayloadCache.data() + eventNameLength, messageLength);
    socket->send(binaryPayloadCache.data(), requiredSize, uWS::BINARY);
}

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
    fmt::print(templateString, args...);
    fmt::print("\n");
}

template<typename... Args>
void Session::log(const std::string& templateString, Args... args) {
    fmt::print(templateString, args...);
    fmt::print("\n");
}

