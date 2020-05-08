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
#include <carta-protobuf/contour.pb.h>
#include <carta-protobuf/file_info.pb.h>
#include <carta-protobuf/file_list.pb.h>
#include <carta-protobuf/moment_request.pb.h>
#include <carta-protobuf/open_file.pb.h>
#include <carta-protobuf/region.pb.h>
#include <carta-protobuf/register_viewer.pb.h>
#include <carta-protobuf/resume_session.pb.h>
#include <carta-protobuf/set_cursor.pb.h>
#include <carta-protobuf/set_image_channels.pb.h>
#include <carta-protobuf/stop_moment_calc.pb.h>
#include <carta-protobuf/tiles.pb.h>
#include <carta-protobuf/user_layout.pb.h>
#include <carta-protobuf/user_preferences.pb.h>

#include <tbb/task.h>

#include "AnimationObject.h"
#include "Catalog/VOTableController.h"
#include "EventHeader.h"
#include "FileList/FileListHandler.h"
#include "FileSettings.h"
#include "Frame.h"
#include "Moment/MomentFilesManager.h"
#include "Moment/MomentGenerator.h"
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
    bool OnOpenFile(const CARTA::OpenFile& message, uint32_t request_id, bool silent = false);
    void OnCloseFile(const CARTA::CloseFile& message);
    void OnAddRequiredTiles(const CARTA::AddRequiredTiles& message, bool skip_data = false);
    void OnSetImageChannels(const CARTA::SetImageChannels& message);
    void OnSetCursor(const CARTA::SetCursor& message, uint32_t request_id);
    bool OnSetRegion(const CARTA::SetRegion& message, uint32_t request_id, bool silent = false);
    void OnRemoveRegion(const CARTA::RemoveRegion& message);
    void OnImportRegion(const CARTA::ImportRegion& message, uint32_t request_id);
    void OnExportRegion(const CARTA::ExportRegion& message, uint32_t request_id);
    void OnSetSpatialRequirements(const CARTA::SetSpatialRequirements& message);
    void OnSetHistogramRequirements(const CARTA::SetHistogramRequirements& message, uint32_t request_id);
    void OnSetSpectralRequirements(const CARTA::SetSpectralRequirements& message);
    void OnSetStatsRequirements(const CARTA::SetStatsRequirements& message);
    void OnSetContourParameters(const CARTA::SetContourParameters& message);
    void OnRegionListRequest(const CARTA::RegionListRequest& request, uint32_t request_id);
    void OnRegionFileInfoRequest(const CARTA::RegionFileInfoRequest& request, uint32_t request_id);

    void OnSetUserPreferences(const CARTA::SetUserPreferences& request, uint32_t request_id);
    void OnSetUserLayout(const CARTA::SetUserLayout& request, uint32_t request_id);

    void OnResumeSession(const CARTA::ResumeSession& message, uint32_t request_id);
    void OnCatalogFileList(CARTA::CatalogListRequest file_list_request, uint32_t request_id);
    void OnCatalogFileInfo(CARTA::CatalogFileInfoRequest file_info_request, uint32_t request_id);
    void OnOpenCatalogFile(CARTA::OpenCatalogFile open_file_request, uint32_t request_id);
    void OnCloseCatalogFile(CARTA::CloseCatalogFile close_file_request);
    void OnCatalogFilter(CARTA::CatalogFilterRequest filter_request, uint32_t request_id);

    void OnMomentRequest(const CARTA::MomentRequest& moment_request, uint32_t request_id);
    void OnStopMomentCalc(const CARTA::StopMomentCalc& stop_moment_calc);

    void SendPendingMessages();
    void AddToSetChannelQueue(CARTA::SetImageChannels message, uint32_t request_id) {
        std::pair<CARTA::SetImageChannels, uint32_t> rp;
        // Empty current queue first.
        while (_set_channel_queues[message.file_id()].try_pop(rp)) {
        }
        _set_channel_queues[message.file_id()].push(std::make_pair(message, request_id));
    }

    // Task handling
    void ExecuteSetChannelEvt(std::pair<CARTA::SetImageChannels, uint32_t> request) {
        OnSetImageChannels(request.first);
    }
    void CancelSetHistRequirements() {
        _histogram_context.cancel_group_execution();
    }
    void ResetHistContext() {
        _histogram_context.reset();
    }
    tbb::task_group_context& HistContext() {
        return _histogram_context;
    }
    tbb::task_group_context& AnimationContext() {
        return _animation_context;
    }
    void CancelAnimation() {
        _animation_object->CancelExecution();
    }
    void BuildAnimationObject(CARTA::StartAnimation& msg, uint32_t request_id);
    bool ExecuteAnimationFrame();
    void ExecuteAnimationFrameInner();
    void StopAnimation(int file_id, const ::CARTA::AnimationFrame& frame);
    void HandleAnimationFlowControlEvt(CARTA::AnimationFlowControl& message);
    int CurrentFlowWindowSize() {
        return _animation_object->CurrentFlowWindowSize();
    }
    void CancelExistingAnimation();
    void CheckCancelAnimationOnFileClose(int file_id);
    void AddCursorSetting(CARTA::SetCursor message, uint32_t request_id) {
        _file_settings.AddCursorSetting(message, request_id);
    }
    void ImageChannelLock(int fileId) {
        _image_channel_mutexes[fileId].lock();
    }
    void ImageChannelUnlock(int fileId) {
        _image_channel_mutexes[fileId].unlock();
    }
    bool ImageChannelTaskTestAndSet(int fileId) {
        if (_image_channel_task_active[fileId]) {
            return true;
        } else {
            _image_channel_task_active[fileId] = true;
            return false;
        }
    }
    void ImageChannelTaskSetIdle(int fileId) {
        _image_channel_task_active[fileId] = false;
    }
    int IncreaseRefCount() {
        return ++_ref_count;
    }
    int DecreaseRefCount() {
        return --_ref_count;
    }
    void DisconnectCalled();
    void ConnectCalled();
    static int NumberOfSessions() {
        return _num_sessions;
    }
    tbb::task_group_context& Context() {
        return _base_context;
    }
    void SetWaitingTask(bool set_wait) {
        _animation_object->_waiting_flow_event = set_wait;
    }
    bool WaitingFlowEvent() {
        return _animation_object->_waiting_flow_event;
    }
    bool AnimationRunning() {
        return _animation_object && !_animation_object->_stop_called;
    }
    int CalculateAnimationFlowWindow();
    static void SetExitTimeout(int secs) {
        _exit_after_num_seconds = secs;
        _exit_when_all_sessions_closed = true;
    }
    static void SetInitExitTimeout(int secs);

    inline uint32_t GetId() {
        return _id;
    }

    // Region data streams
    void RegionDataStreams(int file_id, int region_id);
    bool SendSpectralProfileData(int file_id, int region_id, bool channel_changed = false, bool stokes_changed = false);

    // TODO: should these be public? NO!!!!!!!!
    uint32_t _id;
    FileSettings _file_settings;
    std::unordered_map<int, tbb::concurrent_queue<std::pair<CARTA::SetImageChannels, uint32_t>>> _set_channel_queues;

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
    // Only set channel_changed and stokes_changed if they are the only trigger for new data
    // (i.e. result of SET_IMAGE_CHANNELS) to prevent sending unneeded data streams.
    bool SendSpatialProfileData(int file_id, int region_id, bool stokes_changed = false);
    bool SendRegionHistogramData(int file_id, int region_id, bool channel_changed = false);
    bool SendRegionStatsData(int file_id, int region_id); // update stats in all cases
    bool SendContourData(int file_id);
    void UpdateRegionData(int file_id, bool send_image_histogram = true, bool channel_changed = false, bool stokes_changed = false);

    // Send protobuf messages
    void SendEvent(CARTA::EventType event_type, u_int32_t event_id, google::protobuf::MessageLite& message, bool compress = false);
    void SendFileEvent(int file_id, CARTA::EventType event_type, u_int32_t event_id, google::protobuf::MessageLite& message);
    void SendLogEvent(const std::string& message, std::vector<std::string> tags, CARTA::ErrorSeverity severity);

    uWS::WebSocket<uWS::SERVER>* _socket;
    std::string _api_key;
    std::string _root_folder;
    bool _verbose_logging;

    // File browser
    FileListHandler* _file_list_handler;

    // File info for browser, open file
    std::unique_ptr<CARTA::FileInfo> _file_info;
    std::unique_ptr<CARTA::FileInfoExtended> _file_info_extended;
    std::unique_ptr<carta::FileLoader> _loader;

    // Frame
    std::unordered_map<int, std::unique_ptr<Frame>> _frames; // <file_id, Frame>: one frame per image file
    std::mutex _frame_mutex;                                 // lock frames to create/destroy

    // Catalog controller
    std::unique_ptr<catalog::Controller> _catalog_controller;

    // State for animation functions.
    std::unique_ptr<AnimationObject> _animation_object;

    // Moment image files manager
    std::unique_ptr<carta::MomentFilesManager> _moment_files_manager;

    // Manage image channel
    std::unordered_map<int, std::mutex> _image_channel_mutexes;
    std::unordered_map<int, bool> _image_channel_task_active;

    // Cube histogram progress: 0.0 to 1.0 (complete)
    float _histogram_progress;

    // Outgoing messages
    uS::Async* _outgoing_async;                                          // Notification mechanism when messages are ready
    tbb::concurrent_queue<std::pair<std::vector<char>, bool>> _out_msgs; // message queue <msg, compress>

    // TBB context that enables all tasks associated with a session to be cancelled.
    tbb::task_group_context _base_context;

    // TBB context to cancel histogram calculations.
    tbb::task_group_context _histogram_context;

    tbb::task_group_context _animation_context;

    int _ref_count;
    int _animation_id;
    bool _connected;
    static int _num_sessions;
    static int _exit_after_num_seconds;
    static bool _exit_when_all_sessions_closed;
};

#endif // CARTA_BACKEND__SESSION_H_
