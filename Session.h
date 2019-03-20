//# Session.h: representation of a client connected to a server; processes requests from frontend

#ifndef __CARTA_SESSION_H__
#define __CARTA_SESSION_H__

#include "FileSettings.h"
#include "util.h"

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <unordered_map>
#include <tuple>
#include <vector>

#include <casacore/casa/aips.h>
#include <casacore/casa/OS/File.h>
#include <fmt/format.h>
#include <tbb/concurrent_queue.h>
#include <tbb/concurrent_unordered_map.h>
#include <tbb/atomic.h>
#include <uWS/uWS.h>

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


class Session {
public:
    std::string uuid;
    carta::FileSettings fsettings;
    tbb::concurrent_queue<std::tuple<uint8_t,uint32_t,std::vector<char>>> evtq;
    tbb::concurrent_queue<std::pair<CARTA::SetImageChannels,uint32_t>> aniq;
    
protected:
    // communication
    uWS::WebSocket<uWS::SERVER>* socket;
    std::vector<char> binaryPayloadCache;

    // permissions
    std::unordered_map<std::string, std::vector<std::string> >& permissionsMap;
    bool permissionsEnabled;
    std::string apiKey;

    std::string rootFolder, baseFolder, filelistFolder;
    bool verboseLogging;

    // load for file browser, reuse when open file
    CARTA::FileInfo* selectedFileInfo;
    CARTA::FileInfoExtended* selectedFileInfoExtended;

    // <file_id, Frame>: one frame per image file
    std::unordered_map<int, std::unique_ptr<Frame>> frames;
    // lock frames to create/destroy
    std::mutex frameMutex;
    // flag to send histogram with data
    bool newFrame;


    std::mutex _image_channel_mutex;
    bool _image_channel_task_active;

    // cube histogram progress: 0.0 to 1.0 (complete), -1 (cancel)
    tbb::atomic<float> histogramProgress;

    // Notification mechanism when outgoing messages are ready
    uS::Async *outgoing;

    // Return message queue
    tbb::concurrent_queue<std::vector<char>> out_msgs;

public:
    Session(uWS::WebSocket<uWS::SERVER>* ws,
            std::string uuid,
            std::unordered_map<std::string, std::vector<std::string>>& permissionsMap,
            bool enforcePermissions,
            std::string root,
	    std::string base,
            uS::Async *outgoing,
            bool verbose = false);
    ~Session();

    void addToAniQueue(CARTA::SetImageChannels message, uint32_t requestId);
    void executeOneAniEvt(void);
    void cancel_SetHistReqs() {
      histogramProgress.fetch_and_store(HISTOGRAM_CANCEL);
      sendLogEvent("Histogram cancelled", {"histogram"}, CARTA::ErrorSeverity::INFO);
    }

    void addViewSetting(CARTA::SetImageView message, uint32_t requestId) {
      fsettings.addViewSetting(message, requestId);
    }
    void addCursorSetting(CARTA::SetCursor message, uint32_t requestId) {
      fsettings.addCursorSetting(message, requestId);
    }
    void image_channel_lock() { _image_channel_mutex.lock(); }
    void image_channel_unlock() { _image_channel_mutex.unlock(); }
    inline bool image_channel_task_test_and_set() {
      if( _image_channel_task_active ) return true;
      else {
	_image_channel_task_active= true;
	return false;
      }
    }
    inline void image_channal_task_set_idle() {
      _image_channel_task_active= false;
    }

    
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
    void getRelativePath(std::string& folder);
    void getFileList(CARTA::FileListResponse& fileList, std::string folder);
    bool checkPermissionForDirectory(std:: string prefix);
    bool checkPermissionForEntry(std::string entry);
    std::string getType(casacore::ImageOpener::ImageTypes type); // convert enum to string

    // ICD: File info response
    void resetFileInfo(bool create=false); // delete existing file info ptrs, optionally create new ones
    bool fillFileInfo(CARTA::FileInfo* fileInfo, const std::string& filename);
    bool fillExtendedFileInfo(CARTA::FileInfoExtended* extendedInfo, CARTA::FileInfo* fileInfo,
        const std::string folder, const std::string filename, std::string hdu, std::string& message);

    // ICD: Send data streams
    // raster image data, optionally with histogram
    void sendRasterImageData(int fileId, CARTA::RasterImageData& rasterData,
        std::vector<float>& imageData, CARTA::ImageBounds& bounds, int mip,
        CompressionSettings& compression);
    CARTA::RegionHistogramData* getRegionHistogramData(const int32_t fileId, const int32_t regionId);
    void sendCubeHistogramData(const CARTA::SetHistogramRequirements& message, uint32_t requestId);
    // basic message to update progress
    void createCubeHistogramMessage(CARTA::RegionHistogramData& msg, int fileId, int stokes, float progress);

    // profile data
    void sendSpatialProfileData(int fileId, int regionId);
    void sendSpectralProfileData(int fileId, int regionId);
    void sendRegionStatsData(int fileId, int regionId);

    // Send protobuf messages
    void sendEvent(std::string eventName, u_int64_t eventId, google::protobuf::MessageLite& message);
    void sendFileEvent(int fileId, std::string eventName, u_int64_t eventId,
        google::protobuf::MessageLite& message);
    void sendLogEvent(std::string message, std::vector<std::string> tags, CARTA::ErrorSeverity severity);
};

#endif 
