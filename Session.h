#pragma once

#include <fmt/format.h>
#include <boost/multi_array.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <H5Cpp.h>
#include <H5File.h>
#include <mutex>
#include <stdio.h>
#include <uWS/uWS.h>
#include <proto/fileLoadRequest.pb.h>
#include <proto/regionReadRequest.pb.h>
#include <proto/profileRequest.pb.h>
#include <cstdint>
#include <carta-protobuf/register_viewer.pb.h>
#include <carta-protobuf/file_list.pb.h>
#include <carta-protobuf/file_info.pb.h>
#include <carta-protobuf/open_file.pb.h>
#include <boost/filesystem.hpp>
#include "proto/regionStatsRequest.pb.h"
#include "proto/regionReadResponse.pb.h"

#include "compression.h"
#include "Frame.h"
#include "ctpl.h"

#define MAX_SUBSETS 8
typedef Requests::RegionStatsRequest::ShapeType RegionShapeType;



struct RegionStats {
    float minVal = std::numeric_limits<float>::max();
    float maxVal = -std::numeric_limits<float>::max();
    float mean = 0;
    float stdDev = 0;
    int nanCount = 0;
    int validCount = 0;
};


class Session {
public:
    boost::uuids::uuid uuid;
protected:
    // TODO: clean up frames on session delete
    std::map<int, std::unique_ptr<Frame>> frames;
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
            std::map<std::string, std::vector<std::string>>& permissionsMap,
            std::string folder,
            ctpl::thread_pool& serverThreadPool,
            bool verbose = false);
    //void onRegionReadRequest(const Requests::RegionReadRequest& regionReadRequest, u_int64_t requestId);
    //void onFileLoad(const Requests::FileLoadRequest& fileLoadRequest, u_int64_t requestId);
    //void onProfileRequest(const Requests::ProfileRequest& request, u_int64_t requestId);
    //void onRegionStatsRequest(const Requests::RegionStatsRequest& request, u_int64_t requestId);
    // CARTA ICD
    void onRegisterViewer(const CARTA::RegisterViewer& message, uint64_t requestId);
    void onFileListRequest(const CARTA::FileListRequest& request, uint64_t requestId);
    void onFileInfoRequest(const CARTA::FileInfoRequest& request, uint64_t requestId);
    void onOpenFile(const CARTA::OpenFile& message, uint64_t requestId);
    ~Session();

protected:
    //void updateHistogram();
    //bool loadFile(Frame& frame, const std::string& filename, const std::string& hdu, int defaultChannel = 0);
    //bool loadChannel(Frame& frame, int channel, int stokes);
    //bool loadStats(Frame& frame);
//    std::vector<RegionStats> getRegionStats(Frame&, int fileId, int xMin, int xMax, int yMin, int yMax, int channelMin, int channelMax, int stokes, RegionShapeType shapeType);
//    std::vector<RegionStats> getRegionStatsSwizzled(Frame&, int xMin, int xMax, int yMin, int yMax, int channelMin, int channelMax, int stokes, RegionShapeType shapeType);
//    std::vector<bool> getShapeMask(int xMin, int xMax, int yMin, int yMax, RegionShapeType shapeType);
//    std::vector<float> getXProfile(int y, int channel, int stokes);
//    std::vector<float> getYProfile(int x, int channel, int stokes);
//    std::vector<float> getZProfile(int x, int y, int stokes);
    //std::vector<float> readRegion(const Requests::RegionReadRequest& regionReadRequest, bool meanFilter = true);
    CARTA::FileListResponse getFileList(std::string folder);
    bool fillFileInfo(CARTA::FileInfo* fileInfo, boost::filesystem::path& path, std::string& message);
    bool fillExtendedFileInfo(CARTA::FileInfoExtended* extendedInfo, CARTA::FileInfo* fileInfo, const std::string folder, const std::string filename, std::string hdu, std::string& message);
    bool checkPermissionForDirectory(std:: string prefix);
    bool checkPermissionForEntry(std::string entry);
    void sendEvent(std::string eventName, u_int64_t eventId, google::protobuf::MessageLite& message);
};

