#pragma once

#include <fmt/format.h>
#include <boost/multi_array.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <highfive/H5File.hpp>
#include <mutex>
#include <stdio.h>
#include <uWS/uWS.h>
#include <proto/fileLoadRequest.pb.h>
#include <proto/regionReadRequest.pb.h>
#include <proto/profileRequest.pb.h>
#include "proto/regionStatsRequest.pb.h"
#include "proto/regionReadResponse.pb.h"

#include "compression.h"
#include "ctpl.h"

#define MAX_SUBSETS 8
typedef boost::multi_array<float, 4> Matrix4F;
typedef boost::multi_array<float, 3> Matrix3F;
typedef boost::multi_array<float, 2> Matrix2F;
typedef boost::multi_array<int, 3> Matrix3I;
typedef boost::multi_array<int, 2> Matrix2I;
typedef Requests::RegionStatsRequest::ShapeType RegionShapeType;

struct ChannelStats {
    float minVal;
    float maxVal;
    float mean;
    std::vector<float> percentiles;
    std::vector<float> percentileRanks;
    std::vector<int> histogramBins;
    int64_t nanCount;
};

struct RegionStats {
    float minVal = std::numeric_limits<float>::max();
    float maxVal = -std::numeric_limits<float>::max();
    float mean = 0;
    float stdDev = 0;
    int nanCount = 0;
    int validCount = 0;
};

struct ImageInfo {
    std::string filename;
    std::string unit;
    int depth;
    int width;
    int height;
    int stokes;
    int dimensions;
    std::vector<std::vector<ChannelStats>> channelStats;
};

class Session {
public:
    boost::uuids::uuid uuid;
protected:
    float* currentChannelCache;
    Matrix2F currentChannelCache2D;
    Matrix3F currentChannelCache3D;
    Matrix4F currentChannelCache4D;
    std::vector<float> cachedZProfile;
    std::vector<int> cachedZProfileCoords;
    int currentChannel;
    int currentStokes;
    std::unique_ptr<HighFive::File> file;
    std::map<std::string, HighFive::DataSet> dataSets;

    ImageInfo imageInfo;
    std::mutex eventMutex;
    uWS::WebSocket<uWS::SERVER>* socket;
    std::string apiKey;
    std::map<std::string, std::vector<std::string> >& permissionsMap;
    std::string baseFolder;
    std::vector<char> binaryPayloadCache;
    std::vector<char> compressionBuffers[MAX_SUBSETS];
    std::vector<std::string> availableFileList;
    bool verboseLogging;
    Responses::RegionReadResponse regionReadResponse;
    ctpl::thread_pool& threadPool;
    float rateSum;
    int rateCount;

public:
    Session(uWS::WebSocket<uWS::SERVER>* ws,
            boost::uuids::uuid uuid,
            std::string apiKey,
            std::map<std::string, std::vector<std::string>>& permissionsMap,
            std::string folder,
            ctpl::thread_pool& serverThreadPool,
            bool verbose = false);
    void onRegionReadRequest(const Requests::RegionReadRequest& regionReadRequest);
    void onFileLoad(const Requests::FileLoadRequest& fileLoadRequest);
    void onProfileRequest(const Requests::ProfileRequest& request);
    void onRegionStatsRequest(const Requests::RegionStatsRequest& request);
    ~Session();

protected:
    void updateHistogram();
    bool loadFile(const std::string& filename, int defaultChannel = 0);
    bool loadChannel(int channel, int stokes);
    bool loadStats();
    std::vector<RegionStats> getRegionStats(int xMin, int xMax, int yMin, int yMax, int channelMin, int channelMax, int stokes, RegionShapeType shapeType);
    std::vector<RegionStats> getRegionStatsSwizzled(int xMin, int xMax, int yMin, int yMax, int channelMin, int channelMax, int stokes, RegionShapeType shapeType);
    std::vector<bool> getShapeMask(int xMin, int xMax, int yMin, int yMax, RegionShapeType shapeType);
    std::vector<float> getXProfile(int y, int channel, int stokes);
    std::vector<float> getYProfile(int x, int channel, int stokes);
    std::vector<float> getZProfile(int x, int y, int stokes);
    std::vector<float> readRegion(const Requests::RegionReadRequest& regionReadRequest, bool meanFilter = true);
    std::vector<std::string> getAvailableFiles(const std::string& folder, std::string prefix = "");
    bool checkPermissionForDirectory(std:: string prefix);
    bool checkPermissionForEntry(std::string entry);
    void sendEvent(std::string eventName, google::protobuf::MessageLite& message);
    void log(const std::string& logMessage);
    template<typename... Args>
    void log(const char* templateString, Args... args);
    template<typename... Args>
    void log(const std::string& templateString, Args... args);
};

