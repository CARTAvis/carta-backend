/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// # Session.h: representation of a client connected to a server; processes requests from frontend

#ifndef CARTA_SRC_SESSION_SESSION_H_
#define CARTA_SRC_SESSION_SESSION_H_

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <map>
#include <mutex>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include <uWebSockets/App.h>

#include <casacore/casa/aips.h>

#include "AnimationObject.h"
#include "Cache/LoaderCache.h"
#include "ChannelMap.h"
#include "CursorSettings.h"
#include "FileList/FileListHandler.h"
#include "Frame/Frame.h"
#include "ImageData/StokesFilesConnector.h"
#include "Main/ProgramSettings.h"
#include "Region/RegionHandler.h"
#include "SessionContext.h"
#include "Table/TableController.h"
#include "ThreadingManager/Concurrency.h"
#include "Util/Message.h"

#define HISTOGRAM_CANCEL -1.0
#define UPDATE_HISTOGRAM_PROGRESS_PER_SECONDS 2.0
#define LOADER_CACHE_SIZE 25

namespace carta {

typedef std::function<void(const bool&, const std::string&, const std::string&)> ScriptingResponseCallback;
typedef std::function<void()> ScriptingSessionClosedCallback;

struct PerSocketData {
    uint32_t session_id;
    string address;
};

class Session {
public:
    Session(uWS::WebSocket<false, true, PerSocketData>* ws, uWS::Loop* loop, uint32_t id, std::string address,
        std::shared_ptr<FileListHandler> file_list_handler);
    ~Session();

    // CARTA ICD
    void OnRegisterViewer(const CARTA::RegisterViewer& message, uint16_t icd_version, uint32_t request_id);
    void OnFileListRequest(const CARTA::FileListRequest& request, uint32_t request_id);
    void OnFileInfoRequest(const CARTA::FileInfoRequest& request, uint32_t request_id);
    bool OnOpenFile(const CARTA::OpenFile& message, uint32_t request_id, bool silent = false);
    bool OnOpenFile(int file_id, const string& name, std::shared_ptr<casacore::ImageInterface<casacore::Float>> image,
        CARTA::OpenFileAck* open_file_ack);
    void OnCloseFile(const CARTA::CloseFile& message);
    void OnAddRequiredTiles(const CARTA::AddRequiredTiles& message, int channel = CURRENT_Z, int animation_id = 0, bool skip_data = false);
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
    void OnSetContourParameters(const CARTA::SetContourParameters& message, bool silent = false);
    void OnRegionListRequest(const CARTA::RegionListRequest& request, uint32_t request_id);
    void OnRegionFileInfoRequest(const CARTA::RegionFileInfoRequest& request, uint32_t request_id);
    void OnResumeSession(const CARTA::ResumeSession& message, uint32_t request_id);
    void OnCatalogFileList(CARTA::CatalogListRequest file_list_request, uint32_t request_id);
    void OnCatalogFileInfo(CARTA::CatalogFileInfoRequest file_info_request, uint32_t request_id);
    void OnOpenCatalogFile(CARTA::OpenCatalogFile open_file_request, uint32_t request_id, bool silent = false);
    void OnCloseCatalogFile(CARTA::CloseCatalogFile close_file_request);
    void OnCatalogFilter(CARTA::CatalogFilterRequest filter_request, uint32_t request_id);
    void OnMomentRequest(const CARTA::MomentRequest& moment_request, uint32_t request_id);
    void OnStopMomentCalc(const CARTA::StopMomentCalc& stop_moment_calc);
    void OnSaveFile(const CARTA::SaveFile& save_file, uint32_t request_id);
    bool OnConcatStokesFiles(const CARTA::ConcatStokesFiles& message, uint32_t request_id);
    void OnPvRequest(const CARTA::PvRequest& pv_request, uint32_t request_id);
    void OnStopPvCalc(const CARTA::StopPvCalc& stop_pv_calc);
    void OnFittingRequest(const CARTA::FittingRequest& fitting_request, uint32_t request_id);
    void OnStopFitting(const CARTA::StopFitting& stop_fitting);
    void OnSetVectorOverlayParameters(const CARTA::SetVectorOverlayParameters& message);
    void OnStopPvPreview(const CARTA::StopPvPreview& stop_pv_preview);
    void OnClosePvPreview(const CARTA::ClosePvPreview& close_pv_preview);
    void OnRemoteFileRequest(const CARTA::RemoteFileRequest& message, uint32_t request_id);

    void AddToSetChannelQueue(CARTA::SetImageChannels message, uint32_t request_id) {
        // Image channel mutex has been locked by SessionManager.
        // Set current channel or channel range, clear queue if new channel/range.
        bool clear_queue(true);
        if (message.has_current_range()) {
            if (!_channel_map) {
                _channel_map = std::unique_ptr<ChannelMap>(new ChannelMap(message));
            } else {
                clear_queue = _channel_map->SetChannelMap(message);
            }
        } else {
            if (_channel_map) {
                _channel_map->SetChannelMap(message);
            }
        }

        if (clear_queue) {
            std::pair<CARTA::SetImageChannels, uint32_t> rp;
            while (_set_channel_queues[message.file_id()].try_pop(rp)) {
            }
        }

        if (message.has_required_tiles()) {
            _set_channel_queues[message.file_id()].push(std::make_pair(message, request_id));
        }
    }

    bool IsValidChannelMapTile(int file_id, int channel, int32_t tile) {
        // Check if channel is in channel range and tile is in required tiles for file id.
        // TODO: When user pans, needed tiles may not be in latest required tiles so do not check.
        // We need to know current tiles not just required tiles.
        // return IsInChannelMapRange(file_id, channel) && _channel_map->HasTile(file_id, tile);
        return IsInChannelMapRange(file_id, channel);
    }

    bool IsInChannelMapRange(int file_id, int channel) {
        // Check if channel is in channel map range for file id.
        return _channel_map && _channel_map->IsInChannelRange(file_id, channel);
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
    SessionContext& HistContext() {
        return _histogram_context;
    }
    SessionContext& AnimationContext() {
        return _animation_context;
    }
    void CancelAnimation() {
        _animation_object->CancelExecution();
    }
    void BuildAnimationObject(CARTA::StartAnimation& msg, uint32_t request_id);
    bool ExecuteAnimationFrame();
    void ExecuteAnimationFrameInner(int animation_id);
    void StopAnimation(int file_id, const ::CARTA::AnimationFrame& frame);
    void HandleAnimationFlowControlEvt(CARTA::AnimationFlowControl& message);
    int CurrentFlowWindowSize() {
        return _animation_object->CurrentFlowWindowSize();
    }
    void CancelExistingAnimation();
    void CheckCancelAnimationOnFileClose(int file_id);
    void AddCursorSetting(CARTA::SetCursor message, uint32_t request_id) {
        _cursor_settings.AddCursorSetting(message, request_id);
    }
    void ImageChannelLock(int file_id) {
        _image_channel_mutexes[file_id].lock();
    }
    void ImageChannelUnlock(int file_id) {
        _image_channel_mutexes[file_id].unlock();
    }
    bool ImageChannelTaskTestAndSet(int file_id) {
        if (_image_channel_task_active[file_id]) {
            return true;
        } else {
            _image_channel_task_active[file_id] = true;
            return false;
        }
    }
    void ImageChannelTaskSetIdle(int file_id) {
        _image_channel_task_active[file_id] = false;
    }
    int IncreaseRefCount() {
        return ++_ref_count;
    }
    int DecreaseRefCount() {
        return --_ref_count;
    }
    int GetRefCount() {
        return _ref_count;
    }
    void WaitForTaskCancellation();
    void ConnectCalled();
    static int NumberOfSessions() {
        return _num_sessions;
    }
    SessionContext& Context() {
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

    static void SetExitTimeout(int secs);
    static void SetInitExitTimeout(int secs);

    static void SetControllerDeploymentFlag(bool controller_deployment) {
        _controller_deployment = controller_deployment;
    }

    inline uint32_t GetId() {
        return _id;
    }

    inline std::string GetAddress() {
        return _address;
    }

    // RegionDataStreams
    void RegionDataStreams(int file_id, int region_id);
    bool SendSpectralProfileData(int file_id, int region_id, bool stokes_changed = false);
    bool SendPvPreview(int file_id, int region_id, bool preview_region);
    void StopPvPreviewUpdates(int preview_id);

    CursorSettings _cursor_settings;
    std::unordered_map<int, concurrent_queue<std::pair<CARTA::SetImageChannels, uint32_t>>> _set_channel_queues;

    void SendScriptingRequest(
        CARTA::ScriptingRequest& message, ScriptingResponseCallback callback, ScriptingSessionClosedCallback session_closed_callback);
    void OnScriptingResponse(const CARTA::ScriptingResponse& message, uint32_t request_id);
    void OnScriptingAbort(uint32_t scripting_request_id);
    void CloseAllScriptingRequests();

    void StopImageFileList();
    void StopCatalogFileList();

    void UpdateLastMessageTimestamp();
    std::chrono::high_resolution_clock::time_point GetLastMessageTimestamp();

    // Close cached image if it has been updated
    void CloseCachedImage(const std::string& directory, const std::string& file);
    bool AnimationActive() {
        return _animation_active;
    }
    void SetAnimationActive(bool val) {
        _animation_active = val;
    }

protected:
    // File info for file list (extended info for each hdu_name)
    bool FillExtendedFileInfo(std::map<std::string, CARTA::FileInfoExtended>& hdu_info_map, CARTA::FileInfo& file_info,
        const std::string& folder, const std::string& filename, const std::string& hdu, bool support_aips_beam, std::string& message);
    // File info for open file
    bool FillExtendedFileInfo(CARTA::FileInfoExtended& extended_info, CARTA::FileInfo& file_info, const std::string& folder,
        const std::string& filename, std::string& hdu_name, bool support_aips_beam, std::string& message, std::string& fullname);
    bool FillFileInfo(
        CARTA::FileInfo& file_info, const std::string& folder, const std::string& filename, std::string& fullname, std::string& message);

    // File info for open generated image (not disk image)
    bool FillExtendedFileInfo(CARTA::FileInfoExtended& extended_info, std::shared_ptr<casacore::ImageInterface<float>> image,
        const std::string& filename, std::string& message, std::shared_ptr<FileLoader>& image_loader);

    // Next unused file id for generated images
    inline int GetNextFileId() {
        return _last_file_id + 1;
    }

    // Delete Frame(s)
    void DeleteFrame(int file_id);

    // Specialized for cube; accumulate per-z histograms and send progress messages
    bool CalculateCubeHistogram(int file_id, CARTA::RegionHistogramData& cube_histogram_message);

    // Send data streams
    bool SendContourData(int file_id, bool ignore_empty = true, int channel = CURRENT_Z);
    bool SendSpatialProfileData(int file_id, int region_id);
    void SendSpatialProfileDataByFileId(int file_id);
    void SendSpatialProfileDataByRegionId(int region_id);
    bool SendRegionHistogramData(int file_id, int region_id, bool channel_changed = false);
    bool SendRegionStatsData(int file_id, int region_id);

    void UpdateImageData(int file_id, bool send_image_histogram, bool z_changed, bool stokes_changed);
    void UpdateRegionData(int file_id, int region_id, bool z_changed, bool stokes_changed);
    bool SendVectorFieldData(int file_id);

    // Send protobuf messages
    void SendEvent(CARTA::EventType event_type, u_int32_t event_id, const google::protobuf::MessageLite& message, bool compress = true);
    void SendFileEvent(
        int file_id, CARTA::EventType event_type, u_int32_t event_id, google::protobuf::MessageLite& message, bool compress = true);
    void SendLogEvent(const std::string& message, std::vector<std::string> tags, CARTA::ErrorSeverity severity);

    // uWebSockets
    uWS::WebSocket<false, true, PerSocketData>* _socket;
    uWS::Loop* _loop;

    uint32_t _id;
    std::string _address;

    // File browser
    std::shared_ptr<FileListHandler> _file_list_handler;

    // Loader cache
    LoaderCache _loaders;

    // Frame; key is file_id; shared with RegionHandler for data streams
    std::unordered_map<int, std::shared_ptr<Frame>> _frames;
    int _last_file_id;
    std::mutex _frame_mutex;

    // Suffix for opening multiple remote files
    int _remote_file_index;

    const std::unique_ptr<TableController> _table_controller;

    // Handler for region creation, import/export, requirements, and data
    std::unique_ptr<RegionHandler> _region_handler;

    // State for animation functions.
    std::unique_ptr<AnimationObject> _animation_object;
    volatile bool _animation_active;

    // Individual stokes files connector
    std::unique_ptr<StokesFilesConnector> _stokes_files_connector;

    // Manage image channel and channel maps. Key is file_id.
    std::unordered_map<int, std::mutex> _image_channel_mutexes;
    std::unordered_map<int, bool> _image_channel_task_active;
    std::unique_ptr<ChannelMap> _channel_map;

    // Cube histogram progress: 0.0 to 1.0 (complete)
    float _histogram_progress;

    // message queue <msg, compress>
    concurrent_queue<std::pair<std::vector<char>, bool>> _out_msgs;

    // context that enables all tasks associated with a session to be cancelled.
    SessionContext _base_context;

    // context to cancel histogram calculations.
    SessionContext _histogram_context;

    SessionContext _animation_context;

    std::atomic<int> _ref_count;
    int _sync_id;
    int _animation_id;
    bool _connected;
    static volatile int _num_sessions;
    static int _exit_after_num_seconds;
    static bool _exit_when_all_sessions_closed;
    static bool _controller_deployment;
    static std::thread* _animation_thread;

    // Callbacks for scripting responses from the frontend
    std::unordered_map<int, std::tuple<ScriptingResponseCallback, ScriptingSessionClosedCallback>> _scripting_callbacks;
    std::mutex _scripting_mutex;

    // Timestamp for the last protobuf message
    std::chrono::high_resolution_clock::time_point _last_message_timestamp;

    // Parameters which are copied from the global settings
    std::string _top_level_folder;
    bool _read_only_mode;
    bool _enable_scripting;
};

} // namespace carta

#endif // CARTA_SRC_SESSION_SESSION_H_
