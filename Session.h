//# Session.h: representation of a client connected to a server; processes requests from frontend

#pragma once

#include <fmt/format.h>
#include <mutex>
#include <cstdio>
#include <uWS/uWS.h>
#include <cstdint>
#include <unordered_map>
#include <casacore/casa/aips.h>
#include <casacore/casa/OS/File.h>
#include <tbb/concurrent_queue.h>

#include <carta-protobuf/close_file.pb.h>
#include <carta-protobuf/file_info.pb.h>
#include <carta-protobuf/file_list.pb.h>
#include <carta-protobuf/open_file.pb.h>
#include <carta-protobuf/raster_image.pb.h>
#include <carta-protobuf/region.pb.h>
#include <carta-protobuf/region_histogram.pb.h>
#include <carta-protobuf/register_viewer.pb.h>
#include <carta-protobuf/set_image_channels.pb.h>
#include <carta-protobuf/set_image_view.pb.h>
#include <carta-protobuf/set_cursor.pb.h>

#include "compression.h"
#include "Frame.h"

#define MAX_SUBSETS 8

struct CompressionSettings {
    CARTA::CompressionType type;
    float quality;
    int nsubsets;
};

class Session {
public:
    std::string uuid;
protected:
    // communication
    uWS::WebSocket<uWS::SERVER>* socket;
    std::vector<char> binaryPayloadCache;

    // permissions
    std::unordered_map<std::string, std::vector<std::string> >& permissionsMap;
    bool permissionsEnabled;
    std::string apiKey;

    std::string baseFolder;
    bool verboseLogging;

    // <file_id, Frame>: one frame per image file
    std::unordered_map<int, std::unique_ptr<Frame>> frames;

    // Notification mechanism when outgoing messages are ready
    uS::Async *outgoing;

    // for data compression
    CompressionSettings compressionSettings;

    // Return message queue
    tbb::concurrent_queue<std::vector<char>> out_msgs;

public:
    Session(uWS::WebSocket<uWS::SERVER>* ws,
            std::string uuid,
            std::unordered_map<std::string, std::vector<std::string>>& permissionsMap,
            bool enforcePermissions,
            std::string folder,
            uS::Async *outgoing,
            bool verbose = false);
    ~Session();

    // CARTA ICD
    void onRegisterViewer(const CARTA::RegisterViewer& message, uint32_t requestId);
    void onFileListRequest(const CARTA::FileListRequest& request, uint32_t requestId);
    void onFileInfoRequest(const CARTA::FileInfoRequest& request, uint32_t requestId);
    void onOpenFile(const CARTA::OpenFile& message, uint32_t requestId);
    void onCloseFile(const CARTA::CloseFile& message, uint32_t requestId);
    void onSetImageView(const CARTA::SetImageView& message, uint32_t requestId);
    void onSetImageChannels(const CARTA::SetImageChannels& message, uint32_t requestId);
    void onSetCursor(const CARTA::SetCursor& message, uint32_t requestId);
    void onSetRegion(const CARTA::SetRegion& message, uint32_t requestId);
    void onRemoveRegion(const CARTA::RemoveRegion& message, uint32_t requestId);
    void onSetSpatialRequirements(const CARTA::SetSpatialRequirements& message, uint32_t requestId);
    void onSetHistogramRequirements(const CARTA::SetHistogramRequirements& message, uint32_t requestId);
    void onSetSpectralRequirements(const CARTA::SetSpectralRequirements& message, uint32_t requestId);
    void onSetStatsRequirements(const CARTA::SetStatsRequirements& message, uint32_t requestId);

    void sendPendingMessages();

protected:
    // ICD: File list response
    CARTA::FileListResponse getFileList(std::string folder);
    bool checkPermissionForDirectory(std:: string prefix);
    bool checkPermissionForEntry(std::string entry);

    // ICD: File info response
    bool fillFileInfo(CARTA::FileInfo* fileInfo, const std::string& filename);
    bool fillExtendedFileInfo(CARTA::FileInfoExtended* extendedInfo, CARTA::FileInfo* fileInfo,
        const std::string folder, const std::string filename, std::string hdu, std::string& message);

    // ICD: Send data streams
    // raster image data, optionally with histogram
    void sendRasterImageData(int fileId, uint32_t requestId, CARTA::RegionHistogramData* channelHistogram = nullptr);
    CARTA::RegionHistogramData* getRegionHistogramData(const int32_t fileId, const int32_t regionId=-1);
    // profile data
    void sendSpatialProfileData(int fileId, int regionId);
    void sendSpectralProfileData(int fileId, int regionId);
    void sendRegionStatsData(int fileId, int regionId);

    // data compression
    void setCompression(CARTA::CompressionType type, float quality, int nsubsets);

    // Send protobuf messages
    void sendEvent(std::string eventName, u_int64_t eventId, google::protobuf::MessageLite& message);
    void sendFileEvent(int fileId, std::string eventName, u_int64_t eventId, google::protobuf::MessageLite& message);
    void sendLogEvent(std::string message, std::vector<std::string> tags, CARTA::ErrorSeverity severity);
};

