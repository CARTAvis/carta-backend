//# Session.h: representation of a client connected to a server; processes requests from frontend

#ifndef CARTA_BACKEND__SESSION_H_
#define CARTA_BACKEND__SESSION_H_

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <tbb/atomic.h>
#include <tbb/concurrent_queue.h>
#include <tbb/concurrent_unordered_map.h>
#include <uWS/uWS.h>

#include <casacore/casa/aips.h>

#include <carta-protobuf/close_file.pb.h>
#include <carta-protobuf/file_info.pb.h>
#include <carta-protobuf/file_list.pb.h>
#include <carta-protobuf/open_file.pb.h>
#include <carta-protobuf/region.pb.h>
#include <carta-protobuf/register_viewer.pb.h>
#include <carta-protobuf/set_cursor.pb.h>
#include <carta-protobuf/set_image_channels.pb.h>
#include <carta-protobuf/set_image_view.pb.h>

#include <tbb/task.h>

#include "AnimationObject.h"
#include "EventHeader.h"
#include "FileListHandler.h"
#include "FileSettings.h"
#include "Frame.h"
#include "Util.h"

class Session {
public:
    Session(uWS::WebSocket<uWS::SERVER>* ws, uint32_t id, std::string root, uS::Async* outgoing_async, FileListHandler* file_list_handler,
        bool verbose = false);
    ~Session();

    // CARTA ICD
    void OnRegisterViewer(const CARTA::RegisterViewer& message, uint16_t icd_version, uint32_t request_id);
    void OnFileListRequest(const CARTA::FileListRequest& request, uint32_t request_id);
    void OnFileInfoRequest(const CARTA::FileInfoRequest& request, uint32_t request_id);
    void OnOpenFile(const CARTA::OpenFile& message, uint32_t request_id);
    void OnCloseFile(const CARTA::CloseFile& message);
    void OnSetImageView(const CARTA::SetImageView& message);
    void OnSetImageChannels(const CARTA::SetImageChannels& message);
    void OnSetCursor(const CARTA::SetCursor& message, uint32_t request_id);
    void OnSetRegion(const CARTA::SetRegion& message, uint32_t request_id);
    void OnRemoveRegion(const CARTA::RemoveRegion& message);
    void OnSetSpatialRequirements(const CARTA::SetSpatialRequirements& message);
    void OnSetHistogramRequirements(const CARTA::SetHistogramRequirements& message, uint32_t request_id);
    void OnSetSpectralRequirements(const CARTA::SetSpectralRequirements& message);
    void OnSetStatsRequirements(const CARTA::SetStatsRequirements& message);

    void SendPendingMessages();
    void AddToSetChannelQueue(CARTA::SetImageChannels message, uint32_t request_id) {
        _set_channel_queue.push(std::make_pair(message, request_id));
    }

    // Task handling
    void ExecuteSetChannelEvt(std::pair<CARTA::SetImageChannels, uint32_t> request) {
        OnSetImageChannels(request.first);
    }
    void CancelSetHistRequirements() {
        _histogram_progress.fetch_and_store(HISTOGRAM_CANCEL);
    }
    void BuildAnimationObject(CARTA::StartAnimation& msg, uint32_t request_id);
    bool ExecuteAnimationFrame();
    void ExecuteAnimationFrame_inner(bool stopped);
    void StopAnimation(int file_id, const ::CARTA::AnimationFrame& frame);
    void HandleAnimationFlowControlEvt(CARTA::AnimationFlowControl& message);
    void CheckCancelAnimationOnFileClose(int file_id);
    void AddViewSetting(CARTA::SetImageView message, uint32_t request_id) {
        _file_settings.AddViewSetting(message, request_id);
    }
    void AddCursorSetting(CARTA::SetCursor message, uint32_t request_id) {
        _file_settings.AddCursorSetting(message, request_id);
    }
    void ImageChannelLock() {
        _image_channel_mutex.lock();
    }
    void ImageChannelUnlock() {
        _image_channel_mutex.unlock();
    }
    bool ImageChannelTaskTestAndSet() {
        if (_image_channel_task_active) {
            return true;
        } else {
            _image_channel_task_active = true;
            return false;
        }
    }
    void ImageChannelTaskSetIdle() {
        _image_channel_task_active = false;
    }
    int IncreaseRefCount() {
        return ++_ref_count;
    }
    int DecreaseRefCount() {
        return --_ref_count;
    }
    void DisconnectCalled();
    static int NumberOfSessions() {
        return _num_sessions;
    }
    tbb::task_group_context& context() {
        return _base_context;
    }
    void setWaitingTask_ptr(tbb::task* tsk) {
        _animation_object->_waiting_task = tsk;
    }
    tbb::task* getWaitingTask_ptr() {
        return _animation_object->_waiting_task;
    }
    bool waiting_flow_event() {
        return _animation_object->_waiting_flow_event;
    }

    // TODO: should these be public? NO!!!!!!!!
    uint32_t _id;
    FileSettings _file_settings;
    tbb::concurrent_queue<std::pair<CARTA::SetImageChannels, uint32_t>> _set_channel_queue;

private:
    // File info
    void ResetFileInfo(bool create = false); // delete existing file info ptrs, optionally create new ones
    bool FillExtendedFileInfo(CARTA::FileInfoExtended* extended_info, CARTA::FileInfo* file_info, const std::string& folder,
        const std::string& filename, std::string hdu, std::string& message);

    // Delete Frame(s)
    void DeleteFrame(int file_id);

    // Histogram
    CARTA::RegionHistogramData* GetRegionHistogramData(const int32_t file_id, const int32_t region_id, bool check_current_channel = false);
    bool SendCubeHistogramData(const CARTA::SetHistogramRequirements& message, uint32_t request_id);
    // basic message to update progress
    void CreateCubeHistogramMessage(CARTA::RegionHistogramData& msg, int file_id, int stokes, float progress);

    // Send data streams
    bool SendRasterImageData(int file_id, bool send_histogram = false);
    bool SendSpatialProfileData(int file_id, int region_id, bool check_current_stokes = false);
    bool SendSpectralProfileData(int file_id, int region_id, bool check_current_stokes = false);
    bool SendRegionHistogramData(int file_id, int region_id, bool check_current_channel = false);
    bool SendRegionStatsData(int file_id, int region_id);
    void UpdateRegionData(int file_id, bool channel_changed, bool stokes_changed);

    // Send protobuf messages
    void SendEvent(CARTA::EventType event_type, u_int32_t event_id, google::protobuf::MessageLite& message);
    void SendFileEvent(int file_id, CARTA::EventType event_type, u_int32_t event_id, google::protobuf::MessageLite& message);
    void SendLogEvent(const std::string& message, std::vector<std::string> tags, CARTA::ErrorSeverity severity);

    uWS::WebSocket<uWS::SERVER>* _socket;
    std::string _api_key;
    std::string _root_folder;
    bool _verbose_logging;

    // File browser
    FileListHandler* _file_list_handler;

    // File info for browser, open file
    CARTA::FileInfo* _selected_file_info;
    CARTA::FileInfoExtended* _selected_file_info_extended;

    // Frame
    std::unordered_map<int, std::unique_ptr<Frame>> _frames; // <file_id, Frame>: one frame per image file
    std::mutex _frame_mutex;                                 // lock frames to create/destroy
    bool _new_frame;                                         // flag to send histogram with data

    // State for animation functions.
    std::unique_ptr<AnimationObject> _animation_object;

    // Manage image channel
    std::mutex _image_channel_mutex;
    bool _image_channel_task_active;

    // Cube histogram progress: 0.0 to 1.0 (complete), -1 (cancel)
    tbb::atomic<float> _histogram_progress;

    // Outgoing messages
    uS::Async* _outgoing_async;                         // Notification mechanism when messages are ready
    tbb::concurrent_queue<std::vector<char>> _out_msgs; // message queue

    // TBB context that enables all tasks associated with a session to be cancelled.
    tbb::task_group_context _base_context;

    int _ref_count;
    bool _connected;
    static int _num_sessions;
};

#endif // CARTA_BACKEND__SESSION_H_
