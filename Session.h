#pragma once

#include <fmt/format.h>
#include <boost/multi_array.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <H5Cpp.h>
#include <H5File.h>
#include <mutex>
#include <cstdio>
#include <uWS/uWS.h>
#include <cstdint>
#include <carta-protobuf/register_viewer.pb.h>
#include <carta-protobuf/file_list.pb.h>
#include <carta-protobuf/file_info.pb.h>
#include <carta-protobuf/open_file.pb.h>
#include <carta-protobuf/set_image_view.pb.h>
#include <carta-protobuf/set_image_channels.pb.h>
#include <carta-protobuf/close_file.pb.h>
#include <boost/filesystem.hpp>
#include <carta-protobuf/region_histogram.pb.h>

#include "compression.h"
#include "Frame.h"
#include "ctpl.h"

#define MAX_SUBSETS 8

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
    bool permissionsEnabled;
    std::string baseFolder;
    std::vector<char> binaryPayloadCache;
    std::vector<char> compressionBuffers[MAX_SUBSETS];
    bool verboseLogging;
    ctpl::thread_pool& threadPool;
    float rateSum;
    int rateCount;
    CARTA::CompressionType compressionType;
    float compressionQuality;
    int numSubsets;

public:
    Session(uWS::WebSocket<uWS::SERVER>* ws,
            boost::uuids::uuid uuid,
            std::map<std::string, std::vector<std::string>>& permissionsMap,
            bool enforcePermissions,
            std::string folder,
            ctpl::thread_pool& serverThreadPool,
            bool verbose = false);
    // CARTA ICD
    void onRegisterViewer(const CARTA::RegisterViewer& message, uint32_t requestId);
    void onFileListRequest(const CARTA::FileListRequest& request, uint32_t requestId);
    void onFileInfoRequest(const CARTA::FileInfoRequest& request, uint32_t requestId);
    void onOpenFile(const CARTA::OpenFile& message, uint32_t requestId);
    void onCloseFile(const CARTA::CloseFile& message, uint32_t requestId);
    void onSetImageView(const CARTA::SetImageView& message, uint32_t requestId);
    void onSetImageChannels(const CARTA::SetImageChannels& message, uint32_t requestId);
    ~Session();

protected:
    CARTA::FileListResponse getFileList(std::string folder);
    bool fillFileInfo(CARTA::FileInfo* fileInfo, boost::filesystem::path& path, std::string& message);
    bool fillExtendedFileInfo(CARTA::FileInfoExtended* extendedInfo, CARTA::FileInfo* fileInfo, const std::string folder, const std::string filename, std::string hdu, std::string& message);
    bool checkPermissionForDirectory(std:: string prefix);
    bool checkPermissionForEntry(std::string entry);
    void sendImageData(int fileId, uint32_t requestId, CARTA::RegionHistogramData* channelHistogram = nullptr);
    void sendEvent(std::string eventName, u_int64_t eventId, google::protobuf::MessageLite& message);
    void sendLogEvent(std::string message, std::vector<std::string> tags, CARTA::ErrorSeverity severity);
};

