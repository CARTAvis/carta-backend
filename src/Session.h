/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# Session.h: representation of a client connected to a server; processes requests from frontend

#ifndef CARTA_BACKEND__SESSION_H_
#define CARTA_BACKEND__SESSION_H_

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <tbb/concurrent_queue.h>
#include <tbb/concurrent_unordered_map.h>
#include <tbb/task.h>
#include <uWS/uWS.h>

#include <casacore/casa/aips.h>

#include <carta-protobuf/close_file.pb.h>
#include <carta-protobuf/contour.pb.h>
#include <carta-protobuf/export_region.pb.h>
#include <carta-protobuf/file_info.pb.h>
#include <carta-protobuf/file_list.pb.h>
#include <carta-protobuf/import_region.pb.h>
#include <carta-protobuf/moment_request.pb.h>
#include <carta-protobuf/open_file.pb.h>
#include <carta-protobuf/region.pb.h>
#include <carta-protobuf/register_viewer.pb.h>
#include <carta-protobuf/resume_session.pb.h>
#include <carta-protobuf/scripting.pb.h>
#include <carta-protobuf/set_cursor.pb.h>
#include <carta-protobuf/set_image_channels.pb.h>
#include <carta-protobuf/spectral_line_request.pb.h>
#include <carta-protobuf/stop_moment_calc.pb.h>
#include <carta-protobuf/tiles.pb.h>

#include <carta-scripting-grpc/carta_service.grpc.pb.h>

#include "AnimationObject.h"
#include "EventHeader.h"
#include "FileList/FileListHandler.h"
#include "FileSettings.h"
#include "Frame.h"
#include "Region/RegionHandler.h"
#include "Table/TableController.h"
#include "Util.h"

class Session {
public:
    Session(uWS::WebSocket<uWS::SERVER>* ws, uint32_t id, std::string address, std::string root, std::string base,
        uS::Async* outgoing_async, FileListHandler* file_list_handler, bool verbose = false, bool perflog = false, int grpc_port = -1);
    ~Session();

    // CARTA ICD
    void OnRegisterViewer(const CARTA::RegisterViewer& message, uint16_t icd_version, uint32_t request_id);
    void OnFileListRequest(const CARTA::FileListRequest& request, uint32_t request_id);
    void OnFileInfoRequest(const CARTA::FileInfoRequest& request, uint32_t request_id);
    bool OnOpenFile(const CARTA::OpenFile& message, uint32_t request_id, bool silent = false);
    bool OnOpenFile(const carta::CollapseResult& collapse_result, CARTA::MomentResponse& moment_response, uint32_t request_id);
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
    void OnSetContourParameters(const CARTA::SetContourParameters& message, bool silent = false);
    void OnRegionListRequest(const CARTA::RegionListRequest& request, uint32_t request_id);
    void OnRegionFileInfoRequest(const CARTA::RegionFileInfoRequest& request, uint32_t request_id);
    void OnResumeSession(const CARTA::ResumeSession& message, uint32_t request_id);
    void OnCatalogFileList(CARTA::CatalogListRequest file_list_request, uint32_t request_id);
    void OnCatalogFileInfo(CARTA::CatalogFileInfoRequest file_info_request, uint32_t request_id);
    void OnOpenCatalogFile(CARTA::OpenCatalogFile open_file_request, uint32_t request_id, bool silent = false);
    void OnCloseCatalogFile(CARTA::CloseCatalogFile close_file_request);
    void OnCatalogFilter(CARTA::CatalogFilterRequest filter_request, uint32_t request_id);
    void OnSpectralLineRequest(CARTA::SpectralLineRequest spectral_line_request, uint32_t request_id);

    void OnMomentRequest(const CARTA::MomentRequest& moment_request, uint32_t request_id);
    void OnStopMomentCalc(const CARTA::StopMomentCalc& stop_moment_calc);
    void OnSaveFile(const CARTA::SaveFile& save_file, uint32_t request_id);

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

    inline std::string GetAddress() {
        return _address;
    }

    // RegionDataStreams
    void RegionDataStreams(int file_id, int region_id);
    bool SendSpectralProfileData(int file_id, int region_id, bool stokes_changed = false);

    FileSettings _file_settings;
    std::unordered_map<int, tbb::concurrent_queue<std::pair<CARTA::SetImageChannels, uint32_t>>> _set_channel_queues;

    void SendScriptingRequest(uint32_t scripting_request_id, std::string target, std::string action, std::string parameters, bool async);
    void OnScriptingResponse(const CARTA::ScriptingResponse& message, uint32_t request_id);
    bool GetScriptingResponse(uint32_t scripting_request_id, CARTA::script::ActionReply* reply);

private:
    // File info
    bool FillExtendedFileInfo(CARTA::FileInfoExtended& extended_info, CARTA::FileInfo& file_info, const std::string& folder,
        const std::string& filename, std::string hdu, std::string& message);
    bool FillExtendedFileInfo(CARTA::FileInfoExtended& extended_info, std::shared_ptr<casacore::ImageInterface<float>> image,
        const std::string& filename, std::string& message);

    // Delete Frame(s)
    void DeleteFrame(int file_id);

    // Specialized for cube; accumulate per-channel histograms and send progress messages
    bool CalculateCubeHistogram(int file_id, CARTA::RegionHistogramData& cube_histogram_message);
    void CreateCubeHistogramMessage(CARTA::RegionHistogramData& msg, int file_id, int stokes, float progress);

    // Send data streams
    bool SendContourData(int file_id, bool ignore_empty = true);
    bool SendSpatialProfileData(int file_id, int region_id);
    bool SendRegionHistogramData(int file_id, int region_id);
    bool SendRegionStatsData(int file_id, int region_id);
    void UpdateImageData(int file_id, bool send_image_histogram, bool channel_changed, bool stokes_changed);
    void UpdateRegionData(int file_id, int region_id, bool channel_changed, bool stokes_changed);

    // Send protobuf messages
    void SendEvent(CARTA::EventType event_type, u_int32_t event_id, const google::protobuf::MessageLite& message, bool compress = true);
    void SendFileEvent(
        int file_id, CARTA::EventType event_type, u_int32_t event_id, google::protobuf::MessageLite& message, bool compress = true);
    void SendLogEvent(const std::string& message, std::vector<std::string> tags, CARTA::ErrorSeverity severity);

    uWS::WebSocket<uWS::SERVER>* _socket;
    uint32_t _id;
    std::string _address;
    std::string _root_folder;
    std::string _base_folder;
    bool _verbose_logging;
    bool _performance_logging;
    int _grpc_port;

    // File browser
    FileListHandler* _file_list_handler;

    // Loader for reading image from disk
    std::unique_ptr<carta::FileLoader> _loader;

    // Frame; key is file_id; shared with RegionHandler for data streams
    std::unordered_map<int, std::shared_ptr<Frame>> _frames;
    std::mutex _frame_mutex;

    const std::unique_ptr<carta::TableController> _table_controller;

    // Handler for region creation, import/export, requirements, and data
    std::unique_ptr<carta::RegionHandler> _region_handler;

    // State for animation functions.
    std::unique_ptr<AnimationObject> _animation_object;

    // Manage image channel
    std::unordered_map<int, std::mutex> _image_channel_mutexes;
    std::unordered_map<int, bool> _image_channel_task_active;

    // Cube histogram progress: 0.0 to 1.0 (complete)
    float _histogram_progress;

    // Outgoing messages:
    // Notification mechanism when messages are ready
    uS::Async* _outgoing_async;
    // message queue <msg, compress>
    tbb::concurrent_queue<std::pair<std::vector<char>, bool>> _out_msgs;

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

    // Scripting responses from the client
    std::unordered_map<int, CARTA::ScriptingResponse> _scripting_response;
    std::mutex _scripting_mutex;
};

#endif // CARTA_BACKEND__SESSION_H_
