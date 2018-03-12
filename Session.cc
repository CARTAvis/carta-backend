#include "Session.h"
#include <fstream>
#include <boost/filesystem.hpp>
#include <proto/fileLoadResponse.pb.h>
#include <proto/connectionResponse.pb.h>

using namespace HighFive;
using namespace std;
using namespace uWS;
namespace fs = boost::filesystem;

// Default constructor. Associates a websocket with a UUID and sets the base folder for all files
Session::Session(WebSocket<SERVER>* ws, boost::uuids::uuid uuid, string folder, bool verbose)
    : uuid(uuid),
      currentChannel(-1),
      file(nullptr),
      baseFolder(folder),
      verboseLogging(verbose),
      threadPool(MAX_THREADS),
      rateSum(0),
      rateCount(0),
      socket(ws) {

    eventMutex.lock();
    auto tStart = std::chrono::high_resolution_clock::now();
    availableFileList = getAvailableFiles(baseFolder);
    auto tEnd = std::chrono::high_resolution_clock::now();
    auto dtFileSearch = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart).count();
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
                    string prefix = filePath.string() + "/";
                    // Strip out the leading "./" in subdirectories
                    if (prefix.length() > 2 && prefix.substr(0, 2) == "./") {
                        prefix = prefix.substr(2);
                    }
                    auto dir_files = getAvailableFiles(filePath.string(), prefix);
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
    int channel = (currentChannel == -1) ? imageInfo.depth : currentChannel;
    if (imageInfo.channelStats.count(channel) && imageInfo.channelStats[channel].histogram.bins.size()) {
        currentChannelHistogram = imageInfo.channelStats[channel].histogram;
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

    float minVal = currentChannelCache[0][0][0];
    float maxVal = currentChannelCache[0][0][0];

    for (auto i = 0; i < imageInfo.height; i++) {
        for (auto j = 0; j < imageInfo.width; j++) {
            minVal = fmin(minVal, currentChannelCache[0][i][j]);
            maxVal = fmax(maxVal, currentChannelCache[0][i][j]);
        }
    }

    currentChannelHistogram.N = max(sqrt(imageInfo.width * imageInfo.height), 2.0);
    currentChannelHistogram.binWidth = (maxVal - minVal) / currentChannelHistogram.N;
    currentChannelHistogram.firstBinCenter = minVal + currentChannelHistogram.binWidth / 2.0f;
    currentChannelHistogram.bins.resize(currentChannelHistogram.N);
    memset(currentChannelHistogram.bins.data(), 0, sizeof(int) * currentChannelHistogram.bins.size());
    for (auto i = 0; i < imageInfo.height; i++) {
        for (auto j = 0; j < imageInfo.width; j++) {
            auto v = currentChannelCache[0][i][j];
            if (isnan(v)) {
                continue;
            }
            int bin = min((int) ((v - minVal) / currentChannelHistogram.binWidth), currentChannelHistogram.N - 1);
            currentChannelHistogram.bins[bin]++;
        }
    }

    log("Cached histogram not found. Manually updated");

}

bool Session::loadStats() {
    if (!file || !file->isValid()) {

        log("No file loaded");
        return false;
    }
    if (file->exist("Statistics")) {
        auto statsGroup = file->getGroup("Statistics");
        if (statsGroup.isValid() && statsGroup.exist("MaxVals")) {
            auto dataSet = statsGroup.getDataSet("MaxVals");
            auto dims = dataSet.getSpace().getDimensions();
            if (dims.size() == 1 && dims[0] == imageInfo.depth + 1) {
                vector<float> data;
                dataSet.read(data);
                for (auto i = 0; i < imageInfo.depth + 1; i++) {
                    imageInfo.channelStats[i].maxVal = data[i];
                }
            } else {
                log("Invalid MaxVals statistics");
                return false;
            }
        } else {
            log("Missing MaxVals statistics");
            return false;
        }

        if (statsGroup.isValid() && statsGroup.exist("MinVals")) {
            auto dataSet = statsGroup.getDataSet("MinVals");
            auto dims = dataSet.getSpace().getDimensions();
            if (dims.size() == 1 && dims[0] == imageInfo.depth + 1) {
                vector<float> data;
                dataSet.read(data);
                for (auto i = 0; i < imageInfo.depth + 1; i++) {
                    imageInfo.channelStats[i].minVal = data[i];
                }
            } else {
                log("Invalid MinVals statistics");
                return false;
            }
        } else {
            log("Missing MinVals statistics");
            return false;
        }

        if (statsGroup.isValid() && statsGroup.exist("Means")) {
            auto dataSet = statsGroup.getDataSet("Means");
            auto dims = dataSet.getSpace().getDimensions();
            if (dims.size() == 1 && dims[0] == imageInfo.depth + 1) {
                vector<float> data;
                dataSet.read(data);
                for (auto i = 0; i < imageInfo.depth + 1; i++) {
                    imageInfo.channelStats[i].mean = data[i];
                }
            } else {
                log("Invalid Means statistics");
                return false;
            }
        } else {
            log("Missing Means statistics");
            return false;
        }

        if (statsGroup.isValid() && statsGroup.exist("NaNCounts")) {
            auto dataSet = statsGroup.getDataSet("NaNCounts");
            auto dims = dataSet.getSpace().getDimensions();
            if (dims.size() == 1 && dims[0] == imageInfo.depth + 1) {
                vector<int> data;
                dataSet.read(data);
                for (auto i = 0; i < imageInfo.depth + 1; i++) {
                    imageInfo.channelStats[i].nanCount = data[i];
                }
            } else {
                log("Invalid NaNCounts statistics");
                return false;
            }
        } else {
            log("Missing NaNCounts statistics");
            return false;
        }

        if (statsGroup.exist("Histograms")) {
            auto histogramsGroup = statsGroup.getGroup("Histograms");
            if (histogramsGroup.isValid() && histogramsGroup.exist("BinWidths") && histogramsGroup.exist("FirstCenters")
                && histogramsGroup.exist("Bins")) {
                auto dataSetBinWidths = histogramsGroup.getDataSet("BinWidths");
                auto dataSetFirstCenters = histogramsGroup.getDataSet("FirstCenters");
                auto dataSetBins = histogramsGroup.getDataSet("Bins");
                auto dimsBinWidths = dataSetBinWidths.getSpace().getDimensions();
                auto dimsFirstCenters = dataSetFirstCenters.getSpace().getDimensions();
                auto dimsBins = dataSetBins.getSpace().getDimensions();

                if (dimsBinWidths.size() == 1 && dimsFirstCenters.size() == 1 && dimsBins.size() == 2
                    && dimsBinWidths[0] == imageInfo.depth + 1 && dimsFirstCenters[0] == imageInfo.depth + 1
                    && dimsBins[0] == imageInfo.depth + 1) {
                    vector<float> binWidths;
                    dataSetBinWidths.read(binWidths);
                    vector<float> firstCenters;
                    dataSetFirstCenters.read(firstCenters);
                    vector<vector<int>> bins;
                    dataSetBins.read(bins);
                    int N = bins[0].size();

                    for (auto i = 0; i < imageInfo.depth + 1; i++) {
                        imageInfo.channelStats[i].histogram.N = N;
                        imageInfo.channelStats[i].histogram.binWidth = binWidths[i];
                        imageInfo.channelStats[i].histogram.firstBinCenter = firstCenters[i];
                        imageInfo.channelStats[i].histogram.bins = bins[i];
                    }
                } else {
                    log("Invalid Percentiles statistics");
                    return false;
                }

            } else {
                log("Missing Histograms datasets");
                return false;
            }

        } else {
            log("Missing Histograms group");
            return false;
        }

        if (statsGroup.exist("Percentiles")) {
            auto percentilesGroup = statsGroup.getGroup("Percentiles");
            if (percentilesGroup.isValid() && percentilesGroup.exist("Percentiles") && percentilesGroup.exist("Values")) {
                auto dataSetPercentiles = percentilesGroup.getDataSet("Percentiles");
                auto dataSetValues = percentilesGroup.getDataSet("Values");
                auto dims = dataSetPercentiles.getSpace().getDimensions();
                auto dimsValues = dataSetValues.getSpace().getDimensions();

                if (dims.size() == 1 && dimsValues.size() == 2 && dimsValues[0] == imageInfo.depth + 1 && dimsValues[1] == dims[0]) {
                    vector<float> percentiles;
                    dataSetPercentiles.read(percentiles);
                    vector<vector<float>> vals;
                    dataSetValues.read(vals);

                    for (auto i = 0; i < imageInfo.depth + 1; i++) {
                        imageInfo.channelStats[i].percentiles = percentiles;
                        imageInfo.channelStats[i].percentileVals = vals[i];
                    }
                } else {
                    log("Invalid Percentiles statistics");
                    return false;
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

bool Session::loadChannel(int channel) {

    if (!file || !file->isValid()) {

        log("No file loaded");
        return false;
    } else if (channel >= imageInfo.depth) {
        log(fmt::format("Invalid channel for channel {} in file {}", channel, imageInfo.filename));
        return false;
    }

    if (channel >= 0) {
        dataSets[0].select({channel, 0, 0}, {1, imageInfo.height, imageInfo.width}).read(currentChannelCache);
    } else {
        Matrix2F tmp;
        dataSets[1].select({0, 0}, {imageInfo.height, imageInfo.width}).read(tmp);
        currentChannelCache.resize(boost::extents[1][imageInfo.height][imageInfo.width]);
        currentChannelCache[0] = tmp;
    }
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
        log(fmt::format("Problem loading file {}: File is not in available file list.", filename));
        return false;
    }

    try {
        file.reset(new File(fmt::format("{}/{}", baseFolder, filename), File::ReadOnly));
        vector<string> fileObjectList = file->listObjectNames();
        imageInfo.filename = filename;
        auto group = file->getGroup("Image");
        DataSet dataSet = group.getDataSet("Data");

        auto dims = dataSet.getSpace().getDimensions();
        if (dims.size() != 3) {
            log(fmt::format("Problem loading file {}: Data is not a valid 3D array.", filename));
            return false;
        }

        imageInfo.depth = dims[0];
        imageInfo.height = dims[1];
        imageInfo.width = dims[2];
        dataSets.clear();
        dataSets.push_back(dataSet);

        dataSets.push_back(group.getDataSet("AverageData"));

        loadStats();

        if (group.exist("DataSwizzled")) {
            DataSet dataSetSwizzled = group.getDataSet("DataSwizzled");
            auto swizzledDims = dataSetSwizzled.getSpace().getDimensions();
            if (swizzledDims.size() != 3 || swizzledDims[0] != dims[2]) {
                log(fmt::format("Invalid swizzled data set in file {}, ignoring.", filename));
            } else {
                log(fmt::format("Found valid swizzled data set in file {}.", filename));
                dataSets.emplace_back(dataSetSwizzled);
            }
        } else {
            log(fmt::format("File {} missing optional swizzled data set, using fallback calculation.\n", filename));
        }
        return loadChannel(defaultChannel);
    }
    catch (HighFive::Exception& err) {
        log(fmt::format("Problem loading file {}", filename));
        return false;
    }
}

// Calculates a Z Profile for a given X and Y pixel coordinate
vector<float> Session::getZProfile(int x, int y) {
    if (!file || !file->isValid()) {
        log("No file loaded or invalid session");
        return vector<float>();
    } else if (x >= imageInfo.width || y >= imageInfo.height) {
        log("Z profile out of range");
        return vector<float>();
    }

    try {
        vector<float> profile;

        // The third data set is the swizzled dataset (if it exists)
        if (dataSets.size() == 3) {
            // Even when reading a single slice, since we're selecting a 3D space, we need to read into a 3D data structure
            // and then copy to a 1D vector. This is a bug in HighFive that only occurs if the last dimension is zero
            Matrix3F zP;
            dataSets[2].select({x, y, 0}, {1, 1, imageInfo.depth}).read(zP);
            profile.resize(imageInfo.depth);
            memcpy(profile.data(), zP.data(), imageInfo.depth * sizeof(float));
        } else {
            dataSets[0].select({0, y, x}, {imageInfo.depth, 1, 1}).read(profile);
        }
        return profile;
    }
    catch (HighFive::Exception& err) {
        log(fmt::format("Invalid profile request in file {}", imageInfo.filename));
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

    if (currentChannel != regionReadRequest.channel()) {
        if (!loadChannel(regionReadRequest.channel())) {
            log(fmt::format("Select channel {} is invalid!", regionReadRequest.channel()));
            return vector<float>();
        }
    }

    const int mip = regionReadRequest.mip();
    const int x = regionReadRequest.x();
    const int y = regionReadRequest.y();
    const int height = regionReadRequest.height();
    const int width = regionReadRequest.width();

    if (imageInfo.height < y + height || imageInfo.width < x + width) {
        log(fmt::format("Selected region ({}, {}) -> ({}, {} in channel {} is invalid!",
                        x, y, x + width, y + height, regionReadRequest.channel()));
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
                        float pixVal = currentChannelCache[0][y + j * mip + pixelY][x + i * mip + pixelX];
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
                regionData[j * rowLengthRegion + i] = currentChannelCache[0][y + j * mip][x + i * mip];
            }
        }
    }
    return regionData;
}

// Event response to region read request
void Session::onRegionRead(const Requests::RegionReadRequest& regionReadRequest) {
    // Mutex used to prevent overlapping requests for a single client
    eventMutex.lock();

    // Valid compression precision range: (4-31)
    bool compressed = regionReadRequest.compression() >= 4 && regionReadRequest.compression() < 32;
    auto tStartRead = chrono::high_resolution_clock::now();
    vector<float> regionData = readRegion(regionReadRequest, false);
    auto tEndRead = chrono::high_resolution_clock::now();
    auto dtRead = chrono::duration_cast<std::chrono::microseconds>(tEndRead - tStartRead).count();

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
        regionReadResponse.set_num_values(numValues);


        // Stats for channel average stored in last channelStats entry. This will change when we change the schema
        int channel = (currentChannel == -1) ? imageInfo.depth : currentChannel;
        if (imageInfo.channelStats.count(channel) && imageInfo.channelStats[channel].nanCount != imageInfo.width * imageInfo.height) {
            auto& channelStats = imageInfo.channelStats[channel];
            auto stats = regionReadResponse.mutable_stats();
            stats->set_mean(channelStats.mean);
            stats->set_min_val(channelStats.minVal);
            stats->set_max_val(channelStats.maxVal);
            stats->set_nan_counts(channelStats.nanCount);
            stats->clear_percentiles();
            auto percentiles = stats->mutable_percentiles();
            for (auto& v: channelStats.percentiles) {
                percentiles->add_percentiles(v);
            }
            for (auto& v: channelStats.percentileVals) {
                percentiles->add_values(v);
            }

            if (currentChannelHistogram.bins.size() && !isnan(currentChannelHistogram.firstBinCenter)
                && !isnan(currentChannelHistogram.binWidth)) {
                auto hist = stats->mutable_hist();
                hist->set_first_bin_center(currentChannelHistogram.firstBinCenter);
                hist->set_n(currentChannelHistogram.N);
                hist->set_bin_width(currentChannelHistogram.binWidth);
                hist->set_bins(currentChannelHistogram.bins.data(), currentChannelHistogram.N * sizeof(int));
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

            // Only launch in async (threaded) mode when image size is > 0.1 Mpix and multiple subsets are requested
            auto launchPolicy = (numSubsets > 1 && numRows * rowLength > 1e5) ? launch::async : launch::deferred;
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
            auto dtCompress = chrono::duration_cast<std::chrono::microseconds>(tEndCompress - tStartCompress).count();

            // Don't include all-NaN calculations in the average speed calculation
            if (regionReadResponse.stats().nan_counts() != regionReadResponse.num_values()) {
                rateSum += (float) (numRows * rowLength) / dtCompress;
                rateCount++;
            }

            if (verboseLogging) {
                log(fmt::format("Image data of size {:.1f} kB compressed to {:.1f} kB in {} μs at {:.2f} Mpix/s using {} threads (Average {:.2f} Mpix/s) \n",
                           numRows * rowLength * sizeof(float) / 1e3,
                           accumulate(compressedSizes.begin(), compressedSizes.end(), 0) / 1e3,
                           dtCompress, (float) (numRows * rowLength) / dtCompress, numSubsets, rateSum / max(rateCount, 1)));
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
            auto dtSetImageData = chrono::duration_cast<std::chrono::microseconds>(tEnd - tStart).count();
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
        log(fmt::format("File {} loaded successfully", fileLoadRequest.filename()));

        fileLoadResponse.set_success(true);
        fileLoadResponse.set_filename(fileLoadRequest.filename());
        fileLoadResponse.set_image_width(imageInfo.width);
        fileLoadResponse.set_image_height(imageInfo.height);
        fileLoadResponse.set_image_depth(imageInfo.depth);

    } else {
        log(fmt::format("Error loading file {}", fileLoadRequest.filename()));
        fileLoadResponse.set_success(false);
    }
    eventMutex.unlock();
    sendEvent("fileload", fileLoadResponse);
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
    auto tStart = chrono::high_resolution_clock::now();
    message.SerializeToArray(binaryPayloadCache.data() + eventNameLength, messageLength);
    auto tEnd = chrono::high_resolution_clock::now();
    auto dtSerialize = chrono::duration_cast<std::chrono::microseconds>(tEnd - tStart).count();
    tStart = chrono::high_resolution_clock::now();
    socket->send(binaryPayloadCache.data(), requiredSize, uWS::BINARY);
    tEnd = chrono::high_resolution_clock::now();
    auto dtSend = chrono::duration_cast<std::chrono::microseconds>(tEnd - tStart).count();
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
