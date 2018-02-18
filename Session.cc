#include "Session.h"
#include <fstream>
#include <boost/filesystem.hpp>
#include <algorithm>
#include <proto/fileLoadResponse.pb.h>
#include <proto/connectionResponse.pb.h>

using namespace HighFive;
using namespace std;
using namespace uWS;
using namespace rapidjson;
namespace fs = boost::filesystem;

// Default constructor. Associates a websocket with a UUID and sets the base folder for all files
Session::Session(WebSocket<SERVER> *ws, boost::uuids::uuid uuid, string folder, bool verbose)
    : uuid(uuid),
      currentBand(-1),
      file(nullptr),
      baseFolder(folder),
      verboseLogging(verbose),
      socket(ws),
      binaryPayloadCache(nullptr),
      payloadSizeCached(0) {

  eventMutex.lock();
  auto tStart = std::chrono::high_resolution_clock::now();
  fs::path folderPath(baseFolder);
  try {
    if (fs::exists(folderPath) && fs::is_directory(folderPath)) {
      for (auto &directoryEntry : fs::directory_iterator(folderPath)) {
        fs::path filePath(directoryEntry);
        if (fs::is_regular_file(filePath) && fs::file_size(directoryEntry) > 8) {
          uint64_t sig;
          string filename = directoryEntry.path().string();
          ifstream file(filename, ios::in | ios::binary);
          if (file.is_open()) {
            file.read((char *) &sig, 8);
            if (sig == 0xa1a0a0d46444889) {
              availableFileList.push_back(directoryEntry.path().filename().string());
            }
          }
          file.close();
        }
      }

    }
  }
  catch (const fs::filesystem_error &ex) {
    fmt::print("Error: {}\n", ex.what());
  }
  auto tEnd = std::chrono::high_resolution_clock::now();
  auto dtFileSearch = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart).count();
  fmt::print("Found {} HDF5 files in {} ms\n", availableFileList.size(), dtFileSearch);


  Responses::ConnectionResponse connectionResponse;
  connectionResponse.set_success("true");
  for (auto &v: availableFileList) {
    connectionResponse.add_available_files(v);
  }
  eventMutex.unlock();
  sendEvent("connect", connectionResponse);
}

// Any cached memory freed when session is closed
Session::~Session() {
  delete file;
  delete[] binaryPayloadCache;
}

void Session::updateHistogram() {
  int band = (currentBand==-1) ? imageInfo.depth : currentBand;
  if (imageInfo.bandStats.count(band) && imageInfo.bandStats[band].histogram.bins.size()) {
    currentBandHistogram = imageInfo.bandStats[band].histogram;
    if (currentBand==-1)
      log("Using cached histogram for average band");
    else
      log(fmt::format("Using cached histogram for band {}", currentBand));

    return;
  }

  auto numRows = imageInfo.height;
  if (!numRows)
    return;
  auto rowSize = imageInfo.width;
  if (!rowSize)
    return;

  float minVal = currentBandCache[0][0][0];
  float maxVal = currentBandCache[0][0][0];

  for (auto i = 0; i < imageInfo.height; i++) {
    for (auto j = 0; j < imageInfo.width; j++) {
      minVal = fmin(minVal, currentBandCache[0][i][j]);
      maxVal = fmax(maxVal, currentBandCache[0][i][j]);
    }
  }

  currentBandHistogram.N = max(sqrt(imageInfo.width*imageInfo.height), 2.0);
  currentBandHistogram.binWidth = (maxVal - minVal)/currentBandHistogram.N;
  currentBandHistogram.firstBinCenter = minVal + currentBandHistogram.binWidth/2.0f;
  currentBandHistogram.bins.resize(currentBandHistogram.N);
  memset(currentBandHistogram.bins.data(), 0, sizeof(int)*currentBandHistogram.bins.size());
  for (auto i = 0; i < imageInfo.height; i++) {
    for (auto j = 0; j < imageInfo.width; j++) {
      auto v = currentBandCache[0][i][j];
      if (isnan(v))
        continue;
      int bin = min((int) ((v - minVal)/currentBandHistogram.binWidth), currentBandHistogram.N - 1);
      currentBandHistogram.bins[bin]++;
    }
  }

  log("Updated histogram");

}

bool Session::parseRegionQuery(const Value &message, ReadRegionRequest &regionQuery) {
  const char *intVarNames[] = {"x", "y", "w", "h", "band", "mip", "compression"};

  for (auto varName:intVarNames) {
    if (!message.HasMember(varName) || !message[varName].IsInt())
      return false;
  }

  regionQuery = {message["x"].GetInt(), message["y"].GetInt(), message["w"].GetInt(), message["h"].GetInt(),
                 message["band"].GetInt(), message["mip"].GetInt(), message["compression"].GetInt()};
  if (regionQuery.x < 0 || regionQuery.y < 0 || regionQuery.band < -1 || regionQuery.band >= imageInfo.depth
      || regionQuery.mip < 1 || regionQuery.w < 1 || regionQuery.h < 1)
    return false;
  return true;
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
      if (dims.size()==1 && dims[0]==imageInfo.depth + 1) {
        vector<float> data;
        dataSet.read(data);
        for (auto i = 0; i < imageInfo.depth + 1; i++) {
          imageInfo.bandStats[i].maxVal = data[i];
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
      if (dims.size()==1 && dims[0]==imageInfo.depth + 1) {
        vector<float> data;
        dataSet.read(data);
        for (auto i = 0; i < imageInfo.depth + 1; i++) {
          imageInfo.bandStats[i].minVal = data[i];
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
      if (dims.size()==1 && dims[0]==imageInfo.depth + 1) {
        vector<float> data;
        dataSet.read(data);
        for (auto i = 0; i < imageInfo.depth + 1; i++) {
          imageInfo.bandStats[i].mean = data[i];
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
      if (dims.size()==1 && dims[0]==imageInfo.depth + 1) {
        vector<int> data;
        dataSet.read(data);
        for (auto i = 0; i < imageInfo.depth + 1; i++) {
          imageInfo.bandStats[i].nanCount = data[i];
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

        if (dimsBinWidths.size()==1 && dimsFirstCenters.size()==1 && dimsBins.size()==2
            && dimsBinWidths[0]==imageInfo.depth + 1 && dimsFirstCenters[0]==imageInfo.depth + 1
            && dimsBins[0]==imageInfo.depth + 1) {
          vector<float> binWidths;
          dataSetBinWidths.read(binWidths);
          vector<float> firstCenters;
          dataSetFirstCenters.read(firstCenters);
          vector<vector<int>> bins;
          dataSetBins.read(bins);
          int N = bins[0].size();

          for (auto i = 0; i < imageInfo.depth + 1; i++) {
            imageInfo.bandStats[i].histogram.N = N;
            imageInfo.bandStats[i].histogram.binWidth = binWidths[i];
            imageInfo.bandStats[i].histogram.firstBinCenter = firstCenters[i];
            imageInfo.bandStats[i].histogram.bins = bins[i];
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

        if (dims.size()==1 && dimsValues.size()==2 && dimsValues[0]==imageInfo.depth + 1 && dimsValues[1]==dims[0]) {
          vector<float> percentiles;
          dataSetPercentiles.read(percentiles);
          vector<vector<float>> vals;
          dataSetValues.read(vals);

          for (auto i = 0; i < imageInfo.depth + 1; i++) {
            imageInfo.bandStats[i].percentiles = percentiles;
            imageInfo.bandStats[i].percentileVals = vals[i];
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

bool Session::loadBand(int band) {

  if (!file || !file->isValid()) {

    log("No file loaded");
    return false;
  } else if (band >= imageInfo.depth) {
    log(fmt::format("Invalid band for band {} in file {}", band, imageInfo.filename));
    return false;
  }

  if (band >= 0)
    dataSets[0].select({band, 0, 0}, {1, imageInfo.height, imageInfo.width}).read(currentBandCache);
  else {
    Matrix2F tmp;
    dataSets[1].select({0, 0}, {imageInfo.height, imageInfo.width}).read(tmp);
    currentBandCache.resize(boost::extents[1][imageInfo.height][imageInfo.width]);
    currentBandCache[0] = tmp;
  }
  currentBand = band;
  updateHistogram();
  return true;
}

// Loads a file and the default band.
bool Session::loadFile(const string &filename, int defaultBand) {
  if (filename==imageInfo.filename)
    return true;

  delete file;
  file = nullptr;

  if (find(availableFileList.begin(), availableFileList.end(), filename) == availableFileList.end()){
    log(fmt::format("Problem loading file {}: File is not in available file list.", filename));
    return false;
  }

  try {
    file = new File(fmt::format("{}/{}", baseFolder, filename), File::ReadOnly);
    vector<string> fileObjectList = file->listObjectNames();
    imageInfo.filename = filename;
    auto group = file->getGroup("Image");
    DataSet dataSet = group.getDataSet("Data");

    auto dims = dataSet.getSpace().getDimensions();
    if (dims.size()!=3) {
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
      if (swizzledDims.size()!=3 || swizzledDims[0]!=dims[2]) {
        log(fmt::format("Invalid swizzled data set in file {}, ignoring.", filename));
      } else {
        log(fmt::format("Found valid swizzled data set in file {}.", filename));
        dataSets.emplace_back(dataSetSwizzled);
      }
    } else {
      log(fmt::format("File {} missing optional swizzled data set, using fallback calculation.\n", filename));
    }
    return loadBand(defaultBand);
  }
  catch (HighFive::Exception &err) {
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
    if (dataSets.size()==3) {
      // Even when reading a single slice, since we're selecting a 3D space, we need to read into a 3D data structure
      // and then copy to a 1D vector. This is a bug in HighFive that only occurs if the last dimension is zero
      Matrix3F zP;
      dataSets[2].select({x, y, 0}, {1, 1, imageInfo.depth}).read(zP);
      profile.resize(imageInfo.depth);
      memcpy(profile.data(), zP.data(), imageInfo.depth*sizeof(float));
    } else {
      dataSets[0].select({0, y, x}, {imageInfo.depth, 1, 1}).read(profile);
    }
    return profile;
  }
  catch (HighFive::Exception &err) {
    log(fmt::format("Invalid profile request in file {}", imageInfo.filename));
    return vector<float>();
  }
}

// Reads a region corresponding to the given region request. If the current band is not the same as
// the band specified in the request, the new band is loaded
vector<float> Session::readRegion(const ReadRegionRequest &req, bool meanFilter) {
  if (!file || !file->isValid()) {
    log("No file loaded");
    return vector<float>();
  }

  if (currentBand!=req.band) {
    if (!loadBand(req.band)) {
      log(fmt::format("Select band {} is invalid!", req.band));
      return vector<float>();
    }
  }

  if (imageInfo.height < req.y + req.h || imageInfo.width < req.x + req.w) {
    log(fmt::format("Selected region ({}, {}) -> ({}, {} in band {} is invalid!",
                    req.x,
                    req.y,
                    req.x + req.w,
                    req.x + req.h,
                    req.band));
    return vector<float>();
  }

  size_t numRowsRegion = req.h/req.mip;
  size_t rowLengthRegion = req.w/req.mip;
  vector<float> regionData;
  regionData.resize(numRowsRegion*rowLengthRegion);

  if (meanFilter) {
    // Perform down-sampling by calculating the mean for each MIPxMIP block
    for (auto j = 0; j < numRowsRegion; j++) {
      for (auto i = 0; i < rowLengthRegion; i++) {
        float sumPixel = 0;
        int count = 0;
        for (auto x = 0; x < req.mip; x++) {
          for (auto y = 0; y < req.mip; y++) {
            float pixVal = currentBandCache[0][req.y + j * req.mip + y][req.x + i * req.mip + x];
            if (!isnan(pixVal)) {
              count++;
              sumPixel += pixVal;
            }
          }
        }
        regionData[j * rowLengthRegion + i] = count ? sumPixel / count : NAN;
      }
    }
  }
  else {
    // Nearest neighbour filtering
    for (auto j = 0; j < numRowsRegion; j++) {
      for (auto i = 0; i < rowLengthRegion; i++) {
        regionData[j * rowLengthRegion + i] = currentBandCache[0][req.y + j * req.mip][req.x + i * req.mip];
      }
    }
  }
  return regionData;
}

// Event response to region read request
void Session::onRegionRead(const Value &message) {
  // Mutex used to prevent overlapping requests for a single client
  eventMutex.lock();
  ReadRegionRequest request;
  //Responses::RegionReadResponse regionReadResponse;
  unsigned char *compressionBuffer = nullptr;

  if (parseRegionQuery(message, request)) {
    // Valid compression precision range: (4-31)
    bool compressed = request.compression >= 4 && request.compression < 32;
    vector<float> regionData = readRegion(request, false);

    if (regionData.size()) {
      auto numValues = regionData.size();
      auto rowLength = request.w/request.mip;
      auto numRows = request.h/request.mip;

      regionReadResponse.set_success(true);
      regionReadResponse.set_compression(request.compression);
      regionReadResponse.set_x(request.x);
      regionReadResponse.set_y(request.y);
      regionReadResponse.set_width(rowLength);
      regionReadResponse.set_height(numRows);
      regionReadResponse.set_mip(request.mip);
      regionReadResponse.set_channel(request.band);
      regionReadResponse.set_num_values(numValues);


      // Stats for band average stored in last bandStats entry. This will change when we change the schema
      int band = (currentBand==-1) ? imageInfo.depth : currentBand;
      if (imageInfo.bandStats.count(band) && imageInfo.bandStats[band].nanCount!=imageInfo.width*imageInfo.height) {
        auto &bandStats = imageInfo.bandStats[band];
        auto stats = regionReadResponse.mutable_stats();
        stats->set_mean(bandStats.mean);
        stats->set_min_val(bandStats.minVal);
        stats->set_max_val(bandStats.maxVal);
        stats->set_nan_counts(bandStats.nanCount);
        stats->clear_percentiles();
        auto percentiles = stats->mutable_percentiles();
        for (auto &v: bandStats.percentiles)
          percentiles->add_percentiles(v);
        for (auto &v: bandStats.percentileVals)
          percentiles->add_values(v);

        if (currentBandHistogram.bins.size() && !isnan(currentBandHistogram.firstBinCenter) && !isnan(currentBandHistogram.binWidth)) {
          auto hist = stats->mutable_hist();
          hist->set_first_bin_center(currentBandHistogram.firstBinCenter);
          hist->set_n(currentBandHistogram.N);
          hist->set_bin_width(currentBandHistogram.binWidth);
          hist->set_bins(currentBandHistogram.bins.data(), currentBandHistogram.N*sizeof(int));
        }
        else{
          stats->clear_hist();
        }
      }
      else{
        regionReadResponse.clear_stats();
      }

      if (compressed) {
        // Compression is affected by NaN values, so first remove NaNs and get run-length-encoded NaN list
        auto nanEncoding = getNanEncodings(regionData.data(), regionData.size());
        size_t compressedSize;
        auto tStart = chrono::high_resolution_clock::now();
        compress(regionData.data(), compressionBuffer, compressedSize, rowLength, numRows, request.compression);
        auto tEnd = chrono::high_resolution_clock::now();
        auto dtCompress = chrono::duration_cast<std::chrono::microseconds>(tEnd - tStart).count();
        if (verboseLogging) {
          fmt::print("Image data of size {:.1f} kB compressed to {:.1f} kB in {} μs\n",
                     numRows * rowLength * sizeof(float) / 1e3,
                     compressedSize / 1e3,
                     dtCompress);
        }
        google::protobuf::RepeatedField<int32_t > nanEncodingsField(nanEncoding.begin(), nanEncoding.end());
        regionReadResponse.mutable_nan_encodings()->Swap(&nanEncodingsField);
        regionReadResponse.set_image_data(compressionBuffer, compressedSize);
      } else {
        regionReadResponse.clear_nan_encodings();
        auto tStart = chrono::high_resolution_clock::now();
        regionReadResponse.set_image_data(regionData.data(), numRows*rowLength*sizeof(float));
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
  }
  else {
    log("Event is not a valid ReadRegion request!");
    regionReadResponse.set_success(false);
  }
  eventMutex.unlock();
  sendEvent("region_read", regionReadResponse);
  delete [] compressionBuffer;
}

// Event response to file load request
void Session::onFileLoad(const Value &message) {
  eventMutex.lock();
  Responses::FileLoadResponse fileLoadResponse;
  if (message.HasMember("filename") && message["filename"].IsString()) {
    string filename = message["filename"].GetString();
    if (loadFile(filename)) {
      log(fmt::format("File {} loaded successfully", filename));

      fileLoadResponse.set_success(true);
      fileLoadResponse.set_filename(filename);
      fileLoadResponse.set_image_width(imageInfo.width);
      fileLoadResponse.set_image_height(imageInfo.height);
      fileLoadResponse.set_image_depth(imageInfo.depth);

    } else {
      log(fmt::format("Error loading file {}", filename));
      fileLoadResponse.set_success(false);
    }
  }
  eventMutex.unlock();
  sendEvent("fileload", fileLoadResponse);
}

// Sends an event to the client with a given event name (padded/concatenated to 32 characters) and a given protobuf message
void Session::sendEvent(string eventName, google::protobuf::MessageLite& message) {
  size_t eventNameLength = 32;
  int messageLength = message.ByteSize();
  size_t requiredSize = eventNameLength + messageLength;
  if (!binaryPayloadCache || payloadSizeCached < requiredSize){
    delete [] binaryPayloadCache;
    binaryPayloadCache = new char[requiredSize];
    payloadSizeCached = requiredSize;
  }
  memset(binaryPayloadCache, 0, eventNameLength);
  memcpy(binaryPayloadCache, eventName.c_str(), min(eventName.length(), eventNameLength));
  auto tStart = chrono::high_resolution_clock::now();
  message.SerializeToArray(binaryPayloadCache+eventNameLength, messageLength);
  auto tEnd = chrono::high_resolution_clock::now();
  auto dtSerialize = chrono::duration_cast<std::chrono::microseconds>(tEnd - tStart).count();
  tStart = chrono::high_resolution_clock::now();
  socket->send(binaryPayloadCache, requiredSize, uWS::BINARY);
  tEnd = chrono::high_resolution_clock::now();
  auto dtSend = chrono::duration_cast<std::chrono::microseconds>(tEnd - tStart).count();
  if (verboseLogging){
  if (messageLength>1e4) {
    log(fmt::format("Message of size {:.1f} kB serialised in {} μs\n", messageLength/1e3, dtSerialize));
    log(fmt::format("Event of size {:.1f} kB sent in {} μs\n", requiredSize/1e3, dtSend));
  }
  else{
    log(fmt::format("Message of size {} B serialised in {} μs\n", messageLength, dtSerialize));
    log(fmt::format("Event of size {} B sent in {} μs\n", requiredSize, dtSend));
  }
  }
}

void Session::log(const string &logMessage) {
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
