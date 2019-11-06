#include "Session.h"

#include <signal.h>
#include <algorithm>
#include <chrono>
#include <limits>
#include <memory>
#include <thread>

#include <tbb/parallel_for.h>
#include <tbb/task_group.h>

#include <casacore/casa/OS/File.h>

#include <carta-protobuf/contour_image.pb.h>
#include <carta-protobuf/defs.pb.h>
#include <carta-protobuf/error.pb.h>
#include <carta-protobuf/raster_tile.pb.h>
#include <xmmintrin.h>
#include <zstd.h>

#include "Carta.h"
#include "Compression.h"
#include "EventHeader.h"
#include "FileInfoLoader.h"
#include "InterfaceConstants.h"
#include "OnMessageTask.h"
#include "Util.h"

#define DEBUG(_DB_TEXT_) \
    {}

int Session::_num_sessions = 0;
int Session::_exit_after_num_seconds = 5;
bool Session::_exit_when_all_sessions_closed = false;

// Default constructor. Associates a websocket with a UUID and sets the root folder for all files
Session::Session(uWS::WebSocket<uWS::SERVER>* ws, uint32_t id, std::string root, uS::Async* outgoing_async,
    FileListHandler* file_list_handler, bool verbose)
    : _id(id),
      _socket(ws),
      _root_folder(root),
      _verbose_logging(verbose),
      _selected_file_info(nullptr),
      _selected_file_info_extended(nullptr),
      _outgoing_async(outgoing_async),
      _file_list_handler(file_list_handler),
      _image_channel_task_active(false),
      _file_settings(this) {
    _histogram_progress = HISTOGRAM_COMPLETE;
    _ref_count = 0;
    _animation_object = nullptr;
    _connected = true;

    ++_num_sessions;
    DEBUG(fprintf(stderr, "%p ::Session (%d)\n", this, _num_sessions));
}

static int __exit_backend_timer = 0;

void ExitNoSessions(int s) {
    if (Session::NumberOfSessions() > 0) {
        struct sigaction sig_handler;
        sig_handler.sa_handler = nullptr;
        sigemptyset(&sig_handler.sa_mask);
        sig_handler.sa_flags = 0;
        sigaction(SIGINT, &sig_handler, nullptr);
    } else {
        --__exit_backend_timer;
        if (!__exit_backend_timer) {
            std::cout << "No sessions timeout." << std::endl;
            exit(0);
        }
        alarm(1);
    }
}

Session::~Session() {
    std::unique_lock<std::mutex> lock(_frame_mutex);
    for (auto& frame : _frames) {
        frame.second.reset(); // delete Frame
    }
    _frames.clear();
    _outgoing_async->close();
    --_num_sessions;
    DEBUG(fprintf(stderr, "%p  ~Session (%d)\n", this, _num_sessions));
    if (!_num_sessions) {
        std::cout << "No remaining sessions." << std::endl;
        if (_exit_when_all_sessions_closed) {
            if (_exit_after_num_seconds == 0) {
                std::cout << "Existing due to no sessions remaining" << std::endl;
                exit(0);
            }
            __exit_backend_timer = _exit_after_num_seconds;
            struct sigaction sig_handler;
            sig_handler.sa_handler = ExitNoSessions;
            sigemptyset(&sig_handler.sa_mask);
            sig_handler.sa_flags = 0;
            sigaction(SIGALRM, &sig_handler, nullptr);
            alarm(1);
        }
    }
}

void Session::SetInitExitTimeout(int secs) {
    __exit_backend_timer = secs;
    struct sigaction sig_handler;
    sig_handler.sa_handler = ExitNoSessions;
    sigemptyset(&sig_handler.sa_mask);
    sig_handler.sa_flags = 0;
    sigaction(SIGALRM, &sig_handler, nullptr);
    alarm(1);
}

void Session::DisconnectCalled() {
    _connected = false;
    for (auto& frame : _frames) {
        frame.second->DisconnectCalled(); // call to stop Frame's jobs and wait for jobs finished
    }
    _base_context.cancel_group_execution();
    _histogram_context.cancel_group_execution();
    if (_animation_object) {
        _animation_object->CancelExecution();
    }
}

// ********************************************************************************
// File browser

bool Session::FillExtendedFileInfo(CARTA::FileInfoExtended* extended_info, CARTA::FileInfo* file_info, const string& folder,
    const string& filename, string hdu, string& message) {
    // fill CARTA::FileInfoResponse submessages CARTA::FileInfo and CARTA::FileInfoExtended
    bool ext_file_info_ok(true);
    try {
        file_info->set_name(filename); // in case filename is a link
        casacore::Path root_path(_root_folder);
        root_path.append(folder);
        root_path.append(filename);
        casacore::File cc_file(root_path);
        if (cc_file.exists()) {
            casacore::String full_name(cc_file.path().resolvedName());
            try {
                FileInfoLoader info_loader(full_name);
                if (!info_loader.FillFileInfo(file_info)) {
                    return false;
                }
                if (hdu.empty()) { // use first when required
                    hdu = file_info->hdu_list(0);
                }
                ext_file_info_ok = info_loader.FillFileExtInfo(extended_info, hdu, message);
            } catch (casacore::AipsError& ex) {
                message = ex.getMesg();
                ext_file_info_ok = false;
            }
        } else {
            message = "File " + filename + " does not exist.";
            ext_file_info_ok = false;
        }
    } catch (casacore::AipsError& err) {
        message = err.getMesg();
        ext_file_info_ok = false;
    }
    return ext_file_info_ok;
}

void Session::ResetFileInfo(bool create) {
    // delete old file info pointers
    delete _selected_file_info;
    delete _selected_file_info_extended;
    // optionally create new ones
    if (create) {
        _selected_file_info = new CARTA::FileInfo();
        _selected_file_info_extended = new CARTA::FileInfoExtended();
    } else {
        _selected_file_info = nullptr;
        _selected_file_info_extended = nullptr;
    }
}

// *********************************************************************************
// CARTA ICD implementation

void Session::OnRegisterViewer(const CARTA::RegisterViewer& message, uint16_t icd_version, uint32_t request_id) {
    auto session_id = message.session_id();
    bool success(true);
    std::string status;
    CARTA::SessionType type(CARTA::SessionType::NEW);

    if (icd_version != carta::ICD_VERSION) {
        status = fmt::format("Invalid ICD version number. Expected {}, got {}", carta::ICD_VERSION, icd_version);
        success = false;
    } else if (!session_id) {
        session_id = _id;
        status = fmt::format("Start a new frontend with session id {}", session_id);
    } else {
        type = CARTA::SessionType::RESUMED;
        if (session_id != _id) {
            _id = session_id;
            status = fmt::format("Start a new backend with session id {}", session_id);
        } else {
            status = fmt::format("Network reconnected with session id {}", session_id);
        }
    }

    _api_key = message.api_key();
    // response
    CARTA::RegisterViewerAck ack_message;
    ack_message.set_session_id(session_id);
    ack_message.set_success(success);
    ack_message.set_message(status);
    ack_message.set_session_type(type);
    ack_message.set_server_feature_flags(CARTA::ServerFeatureFlags::SERVER_FEATURE_NONE);
    SendEvent(CARTA::EventType::REGISTER_VIEWER_ACK, request_id, ack_message);
}

void Session::OnFileListRequest(const CARTA::FileListRequest& request, uint32_t request_id) {
    CARTA::FileListResponse response;
    FileListHandler::ResultMsg result_msg;
    _file_list_handler->OnFileListRequest(_api_key, request, response, result_msg);
    SendEvent(CARTA::EventType::FILE_LIST_RESPONSE, request_id, response);
    if (!result_msg.message.empty()) {
        SendLogEvent(result_msg.message, result_msg.tags, result_msg.severity);
    }
}

void Session::OnFileInfoRequest(const CARTA::FileInfoRequest& request, uint32_t request_id) {
    CARTA::FileInfoResponse response;
    auto file_info = response.mutable_file_info();
    auto file_info_extended = response.mutable_file_info_extended();
    string message;
    bool success = FillExtendedFileInfo(file_info_extended, file_info, request.directory(), request.file(), request.hdu(), message);
    if (success) { // save a copy
        ResetFileInfo(true);
        *_selected_file_info = response.file_info();
        *_selected_file_info_extended = response.file_info_extended();
    }
    response.set_success(success);
    response.set_message(message);
    SendEvent(CARTA::EventType::FILE_INFO_RESPONSE, request_id, response);
}

void Session::OnRegionListRequest(const CARTA::RegionListRequest& request, uint32_t request_id) {
    CARTA::RegionListResponse response;
    FileListHandler::ResultMsg result_msg;
    _file_list_handler->OnRegionListRequest(request, response, result_msg);
    SendEvent(CARTA::EventType::REGION_LIST_RESPONSE, request_id, response);
    if (!result_msg.message.empty()) {
        SendLogEvent(result_msg.message, result_msg.tags, result_msg.severity);
    }
}

void Session::OnRegionFileInfoRequest(const CARTA::RegionFileInfoRequest& request, uint32_t request_id) {
    CARTA::RegionFileInfoResponse response;
    FileListHandler::ResultMsg result_msg;
    _file_list_handler->OnRegionFileInfoRequest(request, response, result_msg);
    SendEvent(CARTA::EventType::REGION_FILE_INFO_RESPONSE, request_id, response);
    if (!result_msg.message.empty()) {
        SendLogEvent(result_msg.message, result_msg.tags, result_msg.severity);
    }
}

bool Session::OnOpenFile(const CARTA::OpenFile& message, uint32_t request_id) {
    // Create Frame and send response message
    const auto& directory(message.directory());
    const auto& filename(message.file());
    auto hdu(message.hdu());
    auto file_id(message.file_id());
    // response message:
    CARTA::OpenFileAck ack;
    ack.set_file_id(file_id);
    string err_message;

    bool info_loaded((_selected_file_info != nullptr) && (_selected_file_info_extended != nullptr) &&
                     (_selected_file_info->name() == filename)); // correct file loaded
    if (!info_loaded) {                                          // load from image file
        ResetFileInfo(true);
        info_loaded =
            FillExtendedFileInfo(_selected_file_info_extended, _selected_file_info, message.directory(), message.file(), hdu, err_message);
    }
    bool success(false);
    if (!info_loaded) {
        ResetFileInfo(); // clean up
    } else {
        // Set hdu if empty
        if (hdu.empty()) { // use first
            hdu = _selected_file_info->hdu_list(0);
        } else {
            size_t description_start = hdu.find(" "); // strip ExtName
            if (description_start != std::string::npos) {
                hdu = hdu.substr(0, description_start);
            }
        }
        // form path with filename
        casacore::Path root_path(_root_folder);
        root_path.append(directory);
        root_path.append(filename);
        string abs_filename(root_path.resolvedName());

        // create Frame for open file
        auto frame = std::unique_ptr<Frame>(new Frame(_id, abs_filename, hdu, _selected_file_info_extended, _verbose_logging));
        if (frame->IsValid()) {
            // Check if the old _frames[file_id] object exists. If so, delete it.
            if (_frames.count(file_id) > 0) {
                DeleteFrame(file_id);
            }
            std::unique_lock<std::mutex> lock(_frame_mutex); // open/close lock
            _frames[file_id] = move(frame);
            lock.unlock();
            // copy file info, extended file info
            CARTA::FileInfo* response_file_info = new CARTA::FileInfo();
            response_file_info->set_name(_selected_file_info->name());
            response_file_info->set_type(_selected_file_info->type());
            response_file_info->set_size(_selected_file_info->size());
            response_file_info->add_hdu_list(hdu); // loaded hdu only
            *ack.mutable_file_info() = *response_file_info;
            *ack.mutable_file_info_extended() = *_selected_file_info_extended;
            uint32_t feature_flags = CARTA::FileFeatureFlags::FILE_FEATURE_NONE;
            // TODO: Determine these dynamically. For now, this is hard-coded for all HDF5 features.
            if (_selected_file_info->type() == CARTA::FileType::HDF5) {
                feature_flags |= CARTA::FileFeatureFlags::ROTATED_DATASET;
                feature_flags |= CARTA::FileFeatureFlags::CUBE_HISTOGRAMS;
                feature_flags |= CARTA::FileFeatureFlags::CHANNEL_HISTOGRAMS;
            }
            ack.set_file_feature_flags(feature_flags);

            success = true;
        } else {
            err_message = frame->GetErrorMessage();
        }
    }
    ack.set_success(success);
    ack.set_message(err_message);
    SendEvent(CARTA::EventType::OPEN_FILE_ACK, request_id, ack);
    if (success) {
        UpdateRegionData(file_id);
    }
    return success;
}

void Session::OnCloseFile(const CARTA::CloseFile& message) {
    DeleteFrame(message.file_id());
}

void Session::OnSetImageView(const CARTA::SetImageView& message) {
    if (_frames.count(message.file_id())) {
        _frames.at(message.file_id())
            ->SetImageView(
                message.image_bounds(), message.mip(), message.compression_type(), message.compression_quality(), message.num_subsets());
    }
}

void Session::OnAddRequiredTiles(const CARTA::AddRequiredTiles& message) {
    auto file_id = message.file_id();
    auto channel = _frames.at(file_id)->CurrentChannel();
    auto stokes = _frames.at(file_id)->CurrentStokes();
    if (!message.tiles().empty() && _frames.count(file_id)) {
        int n = message.tiles_size();
        CARTA::CompressionType compression_type = message.compression_type();
        float compression_quality = message.compression_quality();
        int stride = std::min(n, std::min(CARTA::global_thread_count, CARTA::MAX_TILING_TASKS));

        auto lambda = [&](int start) {
            for (int i = start; i < n; i += stride) {
                const auto& encoded_coordinate = message.tiles(i);
                CARTA::RasterTileData raster_tile_data;
                raster_tile_data.set_file_id(file_id);
                auto tile = Tile::Decode(encoded_coordinate);
                if (_frames.at(file_id)->FillRasterTileData(
                        raster_tile_data, tile, channel, stokes, compression_type, compression_quality)) {
                    SendFileEvent(file_id, CARTA::EventType::RASTER_TILE_DATA, 0, raster_tile_data);
                } else {
                    fmt::print("Problem getting tile layer={}, x={}, y={}\n", tile.layer, tile.x, tile.y);
                }
            }
        };

        tbb::task_group g;
        for (int j = 0; j < stride; j++) {
            g.run([=] { lambda(j); });
        }
        g.wait();
    }
}

void Session::OnSetImageChannels(const CARTA::SetImageChannels& message) {
    auto file_id(message.file_id());
    if (_frames.count(file_id)) {
        const std::unique_ptr<Frame>& frame = _frames.at(file_id);
        try {
            std::string err_message;
            auto channel = message.channel();
            auto stokes = message.stokes();
            bool channel_changed(channel != frame->CurrentChannel());
            bool stokes_changed(stokes != frame->CurrentStokes());
            if (frame->SetImageChannels(channel, stokes, err_message)) {
                // RESPONSE: send data for all regions
                bool send_histogram(true);
                UpdateRegionData(file_id, send_histogram, channel_changed, stokes_changed);
                // Send Contour data if required
                SendContourData(file_id);
            } else {
                if (!err_message.empty()) {
                    SendLogEvent(err_message, {"channels"}, CARTA::ErrorSeverity::ERROR);
                }
            }

            // Send any required tiles if they have been requested
            if (message.has_required_tiles()) {
                OnAddRequiredTiles(message.required_tiles());
            }
        } catch (std::out_of_range& range_error) {
            string error = fmt::format("File id {} closed", file_id);
            SendLogEvent(error, {"channels"}, CARTA::ErrorSeverity::DEBUG);
        }
    } else {
        string error = fmt::format("File id {} not found", file_id);
        SendLogEvent(error, {"channels"}, CARTA::ErrorSeverity::DEBUG);
    }
}

void Session::OnSetCursor(const CARTA::SetCursor& message, uint32_t request_id) {
    auto file_id(message.file_id());
    if (_frames.count(file_id)) {
        try {
            if (_frames.at(file_id)->SetCursorRegion(CURSOR_REGION_ID, message.point())) {
                if (_frames.at(file_id)->RegionChanged(CURSOR_REGION_ID)) {
                    // RESPONSE
                    if (message.has_spatial_requirements()) {
                        OnSetSpatialRequirements(message.spatial_requirements());
                        SendSpectralProfileData(file_id, CURSOR_REGION_ID);
                    } else {
                        SendSpatialProfileData(file_id, CURSOR_REGION_ID);
                        SendSpectralProfileData(file_id, CURSOR_REGION_ID);
                    }
                }
            }
        } catch (std::out_of_range& range_error) {
            string error = fmt::format("File id {} closed", file_id);
            SendLogEvent(error, {"cursor"}, CARTA::ErrorSeverity::DEBUG);
        }
    } else {
        string error = fmt::format("File id {} not found", file_id);
        SendLogEvent(error, {"cursor"}, CARTA::ErrorSeverity::DEBUG);
    }
}

bool Session::OnSetRegion(const CARTA::SetRegion& message, uint32_t request_id) {
    // set new Region or update existing one
    auto file_id(message.file_id());
    auto region_id(message.region_id());
    std::string err_message;
    bool success(false);

    if (_frames.count(file_id)) {
        try {
            if (message.region_id() < 0) {    // get region id unique across all frames
                for (auto& frame : _frames) { // frames = map<file_id, unique_ptr<Frame>>
                    region_id = std::max(region_id, frame.second->GetMaxRegionId());
                }
                ++region_id; // get next available
                if (region_id == 0) {
                    ++region_id;
                } // reserved for cursor
            }
            std::vector<CARTA::Point> points = {message.control_points().begin(), message.control_points().end()};
            auto& frame = _frames[file_id]; // use frame in SetRegion message
            success = _frames.at(file_id)->SetRegion(
                region_id, message.region_name(), message.region_type(), points, message.rotation(), err_message);
        } catch (std::out_of_range& range_error) {
            err_message = fmt::format("File id {} closed", file_id);
        }
    } else {
        err_message = fmt::format("File id {} not found", file_id);
    }
    // RESPONSE
    CARTA::SetRegionAck ack;
    ack.set_region_id(region_id);
    ack.set_success(success);
    ack.set_message(err_message);
    SendEvent(CARTA::EventType::SET_REGION_ACK, request_id, ack);
    // update data streams if requirements set
    if (success && _frames.at(file_id)->RegionChanged(region_id)) {
        _frames.at(file_id)->IncreaseZProfileCount(region_id);
        SendRegionStatsData(file_id, region_id);
        SendSpatialProfileData(file_id, region_id);
        SendRegionHistogramData(file_id, region_id);
        SendSpectralProfileData(file_id, region_id);
        _frames.at(file_id)->DecreaseZProfileCount(region_id);
    }
    return success;
}

void Session::OnRemoveRegion(const CARTA::RemoveRegion& message) {
    auto region_id(message.region_id());
    if ((region_id != CURSOR_REGION_ID) && (region_id != IMAGE_REGION_ID)) {
        for (auto& frame : _frames) { // frames = map<fileId, unique_ptr<Frame>>
            if (frame.second) {
                frame.second->RemoveRegion(region_id);
            }
        }
    }
}

void Session::OnImportRegion(const CARTA::ImportRegion& message, uint32_t request_id) {
    auto file_id(message.group_id()); // eventually, import into wcs group
    if (_frames.count(file_id)) {
        try {
            CARTA::FileType file_type(message.type());
            std::string directory(message.directory()), filename(message.file());
            std::vector<std::string> contents = {message.contents().begin(), message.contents().end()};
            CARTA::ImportRegionAck import_ack; // response

            // check for valid file or contents
            bool import_file(!directory.empty() && !filename.empty()), import_contents(!contents.empty());
            if (!import_file && !import_contents) {
                import_ack.set_success(false);
                import_ack.set_message("Import region failed: cannot import by filename or contents.");
                import_ack.add_regions();
                SendFileEvent(file_id, CARTA::EventType::IMPORT_REGION_ACK, request_id, import_ack);
                return;
            }

            std::string abs_filename;
            if (import_file) {
                // form absolute path with filename
                casacore::Path root_path(_root_folder);
                root_path.append(directory);
                root_path.append(filename);
                casacore::File region_file(root_path);
                // check file
                if (!region_file.exists() || !region_file.isReadable()) {
                    import_ack.set_success(false);
                    import_ack.set_message("Import region failed: cannot open file.");
                    import_ack.add_regions();
                    SendFileEvent(file_id, CARTA::EventType::IMPORT_REGION_ACK, request_id, import_ack);
                    return;
                }
                abs_filename = root_path.resolvedName();
            }

            _frames.at(file_id)->ImportRegion(file_type, abs_filename, contents, import_ack);

            // send any errors to log
            std::string ack_message(import_ack.message());
            if (!ack_message.empty()) {
                CARTA::ErrorSeverity level = (import_ack.success() == true ? CARTA::ErrorSeverity::WARNING : CARTA::ErrorSeverity::ERROR);
                SendLogEvent(ack_message, {"import"}, level);
            }
            // send ack message
            SendFileEvent(file_id, CARTA::EventType::IMPORT_REGION_ACK, request_id, import_ack);
        } catch (std::out_of_range& range_error) {
            std::string error = fmt::format("File id {} closed", file_id);
            SendLogEvent(error, {"import"}, CARTA::ErrorSeverity::DEBUG);
        }
    } else {
        std::string error = fmt::format("File id {} not found", file_id);
        SendLogEvent(error, {"import"}, CARTA::ErrorSeverity::DEBUG);
    }
}

void Session::OnExportRegion(const CARTA::ExportRegion& message, uint32_t request_id) {
    auto file_id(message.file_id());
    if (_frames.count(file_id)) {
        try {
            CARTA::ExportRegionAck export_ack; // response
            CARTA::FileType file_type(message.type());
            CARTA::CoordinateType coord_type(message.coord_type());
            std::string directory(message.directory()), filename(message.file());
            std::vector<int> region_ids = {message.region_id().begin(), message.region_id().end()};
            std::string abs_filename;
            if (!directory.empty() && !filename.empty()) { // export file on server
                // form path with filename
                casacore::Path root_path(_root_folder);
                root_path.append(directory);
                root_path.append(filename);
                abs_filename = root_path.absoluteName();
            }
            _frames.at(file_id)->ExportRegion(file_type, coord_type, region_ids, abs_filename, export_ack);
            SendFileEvent(file_id, CARTA::EventType::EXPORT_REGION_ACK, request_id, export_ack);
        } catch (std::out_of_range& range_error) {
            string error = fmt::format("File id {} closed", file_id);
            SendLogEvent(error, {"export"}, CARTA::ErrorSeverity::DEBUG);
        }
    } else {
        string error = fmt::format("File id {} not found", file_id);
        SendLogEvent(error, {"export"}, CARTA::ErrorSeverity::DEBUG);
    }
}

void Session::OnSetSpatialRequirements(const CARTA::SetSpatialRequirements& message) {
    auto file_id(message.file_id());
    if (_frames.count(file_id)) {
        try {
            auto region_id = message.region_id();
            if (_frames.at(file_id)->SetRegionSpatialRequirements(
                    region_id, std::vector<std::string>(message.spatial_profiles().begin(), message.spatial_profiles().end()))) {
                // RESPONSE
                SendSpatialProfileData(file_id, region_id);
            } else {
                string error = fmt::format("Spatial requirements for region id {} failed to validate ", region_id);
                SendLogEvent(error, {"spatial"}, CARTA::ErrorSeverity::ERROR);
            }
        } catch (std::out_of_range& range_error) {
            string error = fmt::format("File id {} closed", file_id);
            SendLogEvent(error, {"spatial"}, CARTA::ErrorSeverity::DEBUG);
        }
    } else {
        string error = fmt::format("File id {} not found", file_id);
        SendLogEvent(error, {"spatial"}, CARTA::ErrorSeverity::DEBUG);
    }
}

void Session::OnSetHistogramRequirements(const CARTA::SetHistogramRequirements& message, uint32_t request_id) {
    auto file_id(message.file_id());
    if (_frames.count(file_id)) {
        try {
            auto region_id = message.region_id();
            if (_frames.at(file_id)->SetRegionHistogramRequirements(
                    region_id, std::vector<CARTA::SetHistogramRequirements_HistogramConfig>(
                                   message.histograms().begin(), message.histograms().end()))) {
                // RESPONSE
                if (region_id == CUBE_REGION_ID) {
                    SendCubeHistogramData(message, request_id);
                } else {
                    SendRegionHistogramData(file_id, region_id);
                }
            } else {
                string error = fmt::format("Histogram requirements for region id {} failed to validate ", region_id);
                SendLogEvent(error, {"histogram"}, CARTA::ErrorSeverity::ERROR);
            }
        } catch (std::out_of_range& range_error) {
            string error = fmt::format("File id {} closed", file_id);
            SendLogEvent(error, {"histogram"}, CARTA::ErrorSeverity::DEBUG);
        }
    } else {
        string error = fmt::format("File id {} not found", file_id);
        SendLogEvent(error, {"histogram"}, CARTA::ErrorSeverity::DEBUG);
    }
}

void Session::OnSetSpectralRequirements(const CARTA::SetSpectralRequirements& message) {
    auto file_id(message.file_id());
    if (_frames.count(file_id)) {
        try {
            auto region_id = message.region_id();
            if (_frames.at(file_id)->SetRegionSpectralRequirements(
                    region_id, std::vector<CARTA::SetSpectralRequirements_SpectralConfig>(
                                   message.spectral_profiles().begin(), message.spectral_profiles().end()))) {
                // RESPONSE
                SendSpectralProfileData(file_id, region_id);
            } else {
                string error = fmt::format("Spectral requirements for region id {} failed to validate ", region_id);
                SendLogEvent(error, {"spectral"}, CARTA::ErrorSeverity::ERROR);
            }
        } catch (std::out_of_range& range_error) {
            string error = fmt::format("File id {} closed", file_id);
            SendLogEvent(error, {"spectral"}, CARTA::ErrorSeverity::DEBUG);
        }
    } else {
        string error = fmt::format("File id {} not found", file_id);
        SendLogEvent(error, {"spectral"}, CARTA::ErrorSeverity::DEBUG);
    }
}

void Session::OnSetStatsRequirements(const CARTA::SetStatsRequirements& message) {
    auto file_id(message.file_id());
    if (_frames.count(file_id)) {
        try {
            auto region_id = message.region_id();
            if (_frames.at(file_id)->SetRegionStatsRequirements(
                    region_id, std::vector<int>(message.stats().begin(), message.stats().end()))) {
                // RESPONSE
                SendRegionStatsData(file_id, region_id);
            } else {
                string error = fmt::format("Stats requirements for region id {} failed to validate ", region_id);
                SendLogEvent(error, {"stats"}, CARTA::ErrorSeverity::ERROR);
            }
        } catch (std::out_of_range& range_error) {
            string error = fmt::format("File id {} closed", file_id);
            SendLogEvent(error, {"stats"}, CARTA::ErrorSeverity::DEBUG);
        }
    } else {
        string error = fmt::format("File id {} not found", file_id);
        SendLogEvent(error, {"stats"}, CARTA::ErrorSeverity::DEBUG);
    }
}

void Session::OnSetContourParameters(const CARTA::SetContourParameters& message) {
    if (_frames.count(message.file_id())) {
        const int num_levels = message.levels_size();
        if (_frames.at(message.file_id())->SetContourParameters(message) && num_levels) {
            SendContourData(message.file_id());
        }
    }
}

// ******** SEND DATA STREAMS *********

CARTA::RegionHistogramData* Session::GetRegionHistogramData(const int32_t file_id, const int32_t region_id, bool check_current_channel) {
    // Create HistogramData message; sent separately or within RasterImageData
    CARTA::RegionHistogramData* histogram_message(nullptr);
    if (_frames.count(file_id)) {
        try {
            histogram_message = new CARTA::RegionHistogramData();
            if (_frames.at(file_id)->FillRegionHistogramData(region_id, histogram_message, check_current_channel)) {
                histogram_message->set_file_id(file_id);
                histogram_message->set_region_id(region_id);
            } else {
                delete histogram_message;
                histogram_message = nullptr;
            }
        } catch (std::out_of_range& range_error) {
            string error = fmt::format("File id {} closed", file_id);
            SendLogEvent(error, {"histogram"}, CARTA::ErrorSeverity::DEBUG);
        }
    } else {
        string error = fmt::format("File id {} not found", file_id);
        SendLogEvent(error, {"histogram"}, CARTA::ErrorSeverity::DEBUG);
    }
    return histogram_message;
}

bool Session::SendCubeHistogramData(const CARTA::SetHistogramRequirements& message, uint32_t request_id) {
    bool data_sent(false);
    auto file_id = message.file_id();
    if (_frames.count(file_id)) {
        try {
            if (message.histograms_size() == 0) { // cancel!
                _histogram_progress = HISTOGRAM_CANCEL;
                _histogram_context.cancel_group_execution();
                SendLogEvent("Histogram cancelled", {"histogram"}, CARTA::ErrorSeverity::INFO);
                return data_sent;
            }
            auto region_id = message.region_id(); // CUBE_REGION_ID
            auto channel = message.histograms(0).channel();
            auto num_bins = message.histograms(0).num_bins();
            int stokes(_frames.at(file_id)->CurrentStokes());
            CARTA::RegionHistogramData histogram_message;
            CreateCubeHistogramMessage(histogram_message, file_id, stokes, 1.0);
            CARTA::Histogram* message_histogram = histogram_message.add_histograms();
            if (_frames.at(file_id)->GetRegionHistogram(region_id, channel, stokes, num_bins, *message_histogram)) {
                // use stored cube histogram
                SendFileEvent(file_id, CARTA::EventType::REGION_HISTOGRAM_DATA, request_id, histogram_message);
                data_sent = true;
            } else if (_frames.at(file_id)->GetImageHistogram(ALL_CHANNELS, stokes, num_bins, *message_histogram)) {
                // use image cube histogram
                SendFileEvent(file_id, CARTA::EventType::REGION_HISTOGRAM_DATA, request_id, histogram_message);
                data_sent = true;
            } else if (_frames.at(file_id)->NumChannels() == 1) {
                int channel_num(0); // use per-channel histogram for channel 0
                if (_frames.at(file_id)->GetRegionHistogram(IMAGE_REGION_ID, channel_num, stokes, num_bins, *message_histogram)) {
                    // use stored channel 0 histogram
                    SendFileEvent(file_id, CARTA::EventType::REGION_HISTOGRAM_DATA, request_id, histogram_message);
                    data_sent = true;
                } else if (_frames.at(file_id)->GetImageHistogram(channel_num, stokes, num_bins, *message_histogram)) {
                    // use image channel 0 histogram
                    SendFileEvent(file_id, CARTA::EventType::REGION_HISTOGRAM_DATA, request_id, histogram_message);
                    data_sent = true;
                } else { // calculate channel 0 histogram
                    float min_val, max_val;
                    if (!_frames.at(file_id)->GetRegionMinMax(IMAGE_REGION_ID, channel_num, stokes, min_val, max_val)) {
                        _frames.at(file_id)->CalcRegionMinMax(IMAGE_REGION_ID, channel_num, stokes, min_val, max_val);
                    }
                    _frames.at(file_id)->CalcRegionHistogram(
                        IMAGE_REGION_ID, channel_num, stokes, num_bins, min_val, max_val, *message_histogram);
                    // send completed histogram
                    SendFileEvent(file_id, CARTA::EventType::REGION_HISTOGRAM_DATA, request_id, histogram_message);
                    data_sent = true;
                }
            } else { // calculate cube histogram
                _histogram_progress = HISTOGRAM_START;
                auto t_start = std::chrono::high_resolution_clock::now();
                // determine cube min and max values
                float cube_min(FLT_MAX), cube_max(FLT_MIN);
                size_t num_channels(_frames.at(file_id)->NumChannels());
                for (size_t chan = 0; chan < num_channels; ++chan) {
                    // min and max for this channel
                    float chan_min, chan_max;
                    if (!_frames.at(file_id)->GetRegionMinMax(IMAGE_REGION_ID, chan, stokes, chan_min, chan_max)) {
                        _frames.at(file_id)->CalcRegionMinMax(IMAGE_REGION_ID, chan, stokes, chan_min, chan_max);
                    }
                    cube_min = std::min(cube_min, chan_min);
                    cube_max = std::max(cube_max, chan_max);

                    // check for cancel
                    if (_histogram_context.is_group_execution_cancelled()) {
                        break;
                    }

                    // check for progress update
                    auto t_end = std::chrono::high_resolution_clock::now();
                    auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();
                    if ((dt / 1e3) > 2.0) {                                 // send progress
                        float this_chan(chan), all_chans(num_channels * 2); // go through chans twice
                        float progress = this_chan / all_chans;
                        CARTA::RegionHistogramData histogram_progress_msg;
                        CreateCubeHistogramMessage(histogram_progress_msg, file_id, stokes, progress);
                        message_histogram = histogram_progress_msg.add_histograms();
                        SendFileEvent(file_id, CARTA::EventType::REGION_HISTOGRAM_DATA, request_id, histogram_progress_msg);
                        t_start = t_end;
                    }
                }
                // save min,max in cube region
                if (!_histogram_context.is_group_execution_cancelled()) {
                    _frames.at(file_id)->SetRegionMinMax(region_id, channel, stokes, cube_min, cube_max);
                }

                // check cancel and proceed
                if (!_histogram_context.is_group_execution_cancelled()) {
                    // send progress message: half done
                    float progress = 0.50;
                    histogram_message.set_progress(progress);
                    SendFileEvent(file_id, CARTA::EventType::REGION_HISTOGRAM_DATA, request_id, histogram_message);

                    // get histogram bins for each channel and accumulate bin counts
                    std::vector<int> cube_bins;
                    CARTA::Histogram chan_histogram; // histogram for each channel
                    for (size_t chan = 0; chan < num_channels; ++chan) {
                        _frames.at(file_id)->CalcRegionHistogram(region_id, chan, stokes, num_bins, cube_min, cube_max, chan_histogram);
                        // add channel bins to cube bins
                        if (chan == 0) {
                            cube_bins = {chan_histogram.bins().begin(), chan_histogram.bins().end()};
                        } else { // add chan histogram bins to cube histogram bins
                            std::transform(chan_histogram.bins().begin(), chan_histogram.bins().end(), cube_bins.begin(), cube_bins.begin(),
                                std::plus<int>());
                        }

                        // check for cancel
                        if (_histogram_context.is_group_execution_cancelled()) {
                            break;
                        }

                        // check for progress update
                        auto t_end = std::chrono::high_resolution_clock::now();
                        auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();
                        if ((dt / 1e3) > 2.0) {                                 // send progress
                            float this_chan(chan), all_chans(num_channels * 2); // go through chans twice
                            progress = 0.5 + (this_chan / all_chans);
                            CARTA::RegionHistogramData histogram_progress_msg;
                            CreateCubeHistogramMessage(histogram_progress_msg, file_id, stokes, progress);
                            message_histogram = histogram_progress_msg.add_histograms();
                            message_histogram->set_channel(ALL_CHANNELS);
                            message_histogram->set_num_bins(chan_histogram.num_bins());
                            message_histogram->set_bin_width(chan_histogram.bin_width());
                            message_histogram->set_first_bin_center(chan_histogram.first_bin_center());
                            *message_histogram->mutable_bins() = {cube_bins.begin(), cube_bins.end()};
                            SendFileEvent(file_id, CARTA::EventType::REGION_HISTOGRAM_DATA, request_id, histogram_progress_msg);
                            t_start = t_end;
                        }
                    }
                    if (!_histogram_context.is_group_execution_cancelled()) {
                        // send completed cube histogram
                        progress = HISTOGRAM_COMPLETE;
                        CARTA::RegionHistogramData final_histogram_message;
                        CreateCubeHistogramMessage(final_histogram_message, file_id, stokes, progress);
                        message_histogram = final_histogram_message.add_histograms();
                        // fill histogram fields from last channel histogram
                        message_histogram->set_channel(ALL_CHANNELS);
                        message_histogram->set_num_bins(chan_histogram.num_bins());
                        message_histogram->set_bin_width(chan_histogram.bin_width());
                        message_histogram->set_first_bin_center(chan_histogram.first_bin_center());
                        *message_histogram->mutable_bins() = {cube_bins.begin(), cube_bins.end()};

                        // save cube histogram
                        _frames.at(file_id)->SetRegionHistogram(region_id, channel, stokes, *message_histogram);
                        // send completed histogram message
                        SendFileEvent(file_id, CARTA::EventType::REGION_HISTOGRAM_DATA, request_id, final_histogram_message);
                        data_sent = true;
                        _histogram_progress = HISTOGRAM_COMPLETE;
                    }
                }
                _histogram_progress = HISTOGRAM_COMPLETE;
            }
        } catch (std::out_of_range& range_error) {
            string error = fmt::format("File id {} closed", file_id);
            SendLogEvent(error, {"histogram"}, CARTA::ErrorSeverity::DEBUG);
        }
    } else {
        string error = fmt::format("File id {} not found", file_id);
        SendLogEvent(error, {"histogram"}, CARTA::ErrorSeverity::DEBUG);
    }
    return data_sent;
}

void Session::CreateCubeHistogramMessage(CARTA::RegionHistogramData& msg, int file_id, int stokes, float progress) {
    // update progress and make new message
    _histogram_progress = progress;
    msg.set_file_id(file_id);
    msg.set_region_id(CUBE_REGION_ID);
    msg.set_stokes(stokes);
    msg.set_progress(progress);
}

bool Session::SendRasterImageData(int file_id, bool send_histogram) {
    // return true if data sent
    bool data_sent(false);

    if (_frames.count(file_id)) {
        try {
            CARTA::RasterImageData raster_data;
            raster_data.set_file_id(file_id);
            std::string message;
            if (_frames.at(file_id)->FillRasterImageData(raster_data, message)) {
                if (send_histogram) {
                    CARTA::RegionHistogramData* histogram_data = GetRegionHistogramData(file_id, IMAGE_REGION_ID);
                    raster_data.set_allocated_channel_histogram_data(histogram_data);
                }
                SendFileEvent(file_id, CARTA::EventType::RASTER_IMAGE_DATA, 0, raster_data);
            } else {
                SendLogEvent(message, {"raster"}, CARTA::ErrorSeverity::ERROR);
            }
        } catch (std::out_of_range& range_error) {
            string error = fmt::format("File id {} closed", file_id);
            SendLogEvent(error, {"spatial"}, CARTA::ErrorSeverity::DEBUG);
        }
    } else {
        string error = fmt::format("File id {} not found", file_id);
        SendLogEvent(error, {"spatial"}, CARTA::ErrorSeverity::DEBUG);
    }
    return data_sent;
}

bool Session::SendSpatialProfileData(int file_id, int region_id, bool stokes_changed) {
    // return true if data sent
    bool data_sent(false);
    if (_frames.count(file_id)) {
        try {
            if (region_id == CURSOR_REGION_ID && !_frames.at(file_id)->IsCursorSet()) {
                return data_sent; // do not send profile unless frontend set cursor
            }
            CARTA::SpatialProfileData spatial_profile_data;
            if (_frames.at(file_id)->FillSpatialProfileData(region_id, spatial_profile_data, stokes_changed)) {
                spatial_profile_data.set_file_id(file_id);
                spatial_profile_data.set_region_id(region_id);
                SendFileEvent(file_id, CARTA::EventType::SPATIAL_PROFILE_DATA, 0, spatial_profile_data);
                data_sent = true;
            }
        } catch (std::out_of_range& range_error) {
            string error = fmt::format("File id {} closed", file_id);
            SendLogEvent(error, {"spatial"}, CARTA::ErrorSeverity::DEBUG);
        }
    } else {
        string error = fmt::format("File id {} not found", file_id);
        SendLogEvent(error, {"spatial"}, CARTA::ErrorSeverity::DEBUG);
    }
    return data_sent;
}

bool Session::SendSpectralProfileData(int file_id, int region_id, bool channel_changed, bool stokes_changed) {
    // return true if data sent
    bool data_sent(false);
    if (_frames.count(file_id)) {
        try {
            if (region_id == CURSOR_REGION_ID && !_frames.at(file_id)->IsCursorSet()) {
                return data_sent; // do not send profile unless frontend set cursor
            }
            _frames.at(file_id)->IncreaseZProfileCount(region_id);
            bool profile_ok = _frames.at(file_id)->FillSpectralProfileData(
                [&](CARTA::SpectralProfileData profile_data) {
                    if (profile_data.profiles_size() > 0) { // update needed (not for new channel)
                        profile_data.set_file_id(file_id);
                        profile_data.set_region_id(region_id);
                        // send (partial) profile data to the frontend
                        SendFileEvent(file_id, CARTA::EventType::SPECTRAL_PROFILE_DATA, 0, profile_data);
                    }
                },
                region_id, channel_changed, stokes_changed);
            _frames.at(file_id)->DecreaseZProfileCount(region_id);
            if (profile_ok) {
                data_sent = true;
            }
        } catch (std::out_of_range& range_error) {
            string error = fmt::format("File id {} closed", file_id);
            SendLogEvent(error, {"spectral"}, CARTA::ErrorSeverity::DEBUG);
        }
    } else {
        string error = fmt::format("File id {} not found", file_id);
        SendLogEvent(error, {"spectral"}, CARTA::ErrorSeverity::DEBUG);
    }
    return data_sent;
}

bool Session::SendRegionHistogramData(int file_id, int region_id, bool channel_changed) {
    // return true if data sent
    bool data_sent(false);
    std::unique_ptr<CARTA::RegionHistogramData> histogram_data(GetRegionHistogramData(file_id, region_id, channel_changed));
    if (histogram_data != nullptr) { // RESPONSE
        SendFileEvent(file_id, CARTA::EventType::REGION_HISTOGRAM_DATA, 0, *histogram_data);
        data_sent = true;
    }
    return data_sent;
}

bool Session::SendRegionStatsData(int file_id, int region_id) {
    // return true if data sent
    bool data_sent(false);
    if (_frames.count(file_id)) {
        try {
            CARTA::RegionStatsData region_stats_data;
            if (_frames.at(file_id)->FillRegionStatsData(region_id, region_stats_data)) {
                region_stats_data.set_file_id(file_id);
                region_stats_data.set_region_id(region_id);
                SendFileEvent(file_id, CARTA::EventType::REGION_STATS_DATA, 0, region_stats_data);
                data_sent = true;
            }
        } catch (std::out_of_range& range_error) {
            string error = fmt::format("File id {} closed", file_id);
            SendLogEvent(error, {"spectral"}, CARTA::ErrorSeverity::DEBUG);
        }
    }
    return data_sent;
}

bool Session::SendContourData(int file_id) {
    if (_frames.count(file_id)) {
        std::unique_ptr<Frame>& frame = _frames.at(file_id);
        const ContourSettings settings = frame->GetContourParameters();
        int num_levels = settings.levels.size();

        if (!num_levels) {
            return false;
        }

        int64_t total_vertices = 0;

        auto callback = [&](double level, double progress, const std::vector<float>& vertices, const std::vector<int>& indices) {
            CARTA::ContourImageData partial_response;
            partial_response.set_file_id(file_id);
            // Currently only supports identical reference file IDs
            partial_response.set_reference_file_id(settings.reference_file_id);
            partial_response.set_channel(frame->CurrentChannel());
            partial_response.set_stokes(frame->CurrentStokes());
            partial_response.set_progress(progress);

            std::vector<char> compression_buffer;
            const float pixel_rounding = std::max(1, std::min(32, settings.decimation));
            const int compression_level = std::max(0, std::min(20, settings.compression_level));

            // Fill contour set
            auto contour_set = partial_response.add_contour_sets();
            contour_set->set_level(level);

            const int N = vertices.size();
            total_vertices += N;

            if (N) {
                if (compression_level < 1) {
                    contour_set->set_raw_coordinates(vertices.data(), N * sizeof(float));
                    contour_set->set_uncompressed_coordinates_size(N * sizeof(float));
                    contour_set->set_raw_start_indices(indices.data(), indices.size() * sizeof(int32_t));
                    contour_set->set_decimation_factor(0);
                } else {
                    std::vector<int32_t> vertices_shuffled;
                    RoundAndEncodeVertices(vertices, vertices_shuffled, pixel_rounding);

                    // Compress using Zstd library
                    const size_t src_size = N * sizeof(int32_t);
                    compression_buffer.resize(ZSTD_compressBound(src_size));
                    size_t compressed_size = ZSTD_compress(
                        compression_buffer.data(), compression_buffer.size(), vertices_shuffled.data(), src_size, compression_level);

                    contour_set->set_raw_coordinates(compression_buffer.data(), compressed_size);
                    contour_set->set_raw_start_indices(indices.data(), indices.size() * sizeof(int32_t));
                    contour_set->set_uncompressed_coordinates_size(src_size);
                    contour_set->set_decimation_factor(pixel_rounding);
                }
            }
            SendFileEvent(partial_response.file_id(), CARTA::EventType::CONTOUR_IMAGE_DATA, 0, partial_response);
        };

        if (frame->ContourImage(callback)) {
            return true;
        } else {
            SendLogEvent("Error processing contours", {"contours"}, CARTA::ErrorSeverity::WARNING);
        }
    }
    return false;
}

void Session::UpdateRegionData(int file_id, bool send_image_histogram, bool channel_changed, bool stokes_changed) {
    // Send updated data for all regions with requirements; do not send image histogram if already sent with raster data.
    // Only set channel_changed and stokes_changed if they are the only trigger for new data,
    // to prevent sending unneeded data streams.
    if (_frames.count(file_id)) {
        std::vector<int> regions(_frames.at(file_id)->GetRegionIds());
        for (auto region_id : regions) {
            // CHECK FOR CANCEL HERE ??
            SendRegionStatsData(file_id, region_id);
            SendSpatialProfileData(file_id, region_id, stokes_changed);
            if ((region_id == IMAGE_REGION_ID) && send_image_histogram) {
                SendRegionHistogramData(file_id, region_id, channel_changed);
            } else if (region_id != IMAGE_REGION_ID) {
                SendRegionHistogramData(file_id, region_id, channel_changed);
            }
            _frames.at(file_id)->IncreaseZProfileCount(region_id);
            SendSpectralProfileData(file_id, region_id, channel_changed, stokes_changed);
            _frames.at(file_id)->DecreaseZProfileCount(region_id);
        }
    }
}

// *********************************************************************************
// SEND uWEBSOCKET MESSAGES

// Sends an event to the client with a given event name (padded/concatenated to 32 characters) and a given ProtoBuf message
void Session::SendEvent(CARTA::EventType event_type, uint32_t event_id, google::protobuf::MessageLite& message) {
    int message_length = message.ByteSize();
    size_t required_size = message_length + sizeof(carta::EventHeader);
    std::vector<char> msg(required_size, 0);
    carta::EventHeader* head = (carta::EventHeader*)msg.data();

    head->type = event_type;
    head->icd_version = carta::ICD_VERSION;
    head->request_id = event_id;
    message.SerializeToArray(msg.data() + sizeof(carta::EventHeader), message_length);
    _out_msgs.push(msg);
    _outgoing_async->send();
}

void Session::SendFileEvent(int32_t file_id, CARTA::EventType event_type, uint32_t event_id, google::protobuf::MessageLite& message) {
    // do not send if file is closed
    if (_frames.count(file_id)) {
        SendEvent(event_type, event_id, message);
    }
}

void Session::SendPendingMessages() {
    // Do not parallelize: this must be done serially
    // due to the constraints of uWS.
    std::vector<char> msg;
    if (_connected) {
        while (_out_msgs.try_pop(msg)) {
            _socket->send(msg.data(), msg.size(), uWS::BINARY);
        }
    }
}

void Session::SendLogEvent(const std::string& message, std::vector<std::string> tags, CARTA::ErrorSeverity severity) {
    CARTA::ErrorData error_data;
    error_data.set_message(message);
    error_data.set_severity(severity);
    *error_data.mutable_tags() = {tags.begin(), tags.end()};
    SendEvent(CARTA::EventType::ERROR_DATA, 0, error_data);
    if ((severity > CARTA::ErrorSeverity::DEBUG) || _verbose_logging) {
        Log(_id, message);
    }
}

void Session::BuildAnimationObject(CARTA::StartAnimation& msg, uint32_t request_id) {
    CARTA::AnimationFrame start_frame, first_frame, last_frame, delta_frame;
    int file_id;
    uint32_t frame_rate;
    bool looping, reverse_at_end, always_wait;

    start_frame = msg.start_frame();
    first_frame = msg.first_frame();
    last_frame = msg.last_frame();
    delta_frame = msg.delta_frame();
    file_id = msg.file_id();
    frame_rate = msg.frame_rate();
    looping = msg.looping();
    reverse_at_end = msg.reverse();
    always_wait = true;

    OnSetImageView(msg.imageview());

    _animation_object = std::unique_ptr<AnimationObject>(
        new AnimationObject(file_id, start_frame, first_frame, last_frame, delta_frame, frame_rate, looping, reverse_at_end, always_wait));

    CARTA::StartAnimationAck ack_message;
    ack_message.set_success(true);
    ack_message.set_message("Starting animation");
    SendEvent(CARTA::EventType::START_ANIMATION_ACK, request_id, ack_message);
}

void Session::ExecuteAnimationFrameInner(bool stopped) {
    CARTA::AnimationFrame curr_frame;

    if (stopped) {
        if (((_animation_object->_stop_frame.channel() == _animation_object->_current_frame.channel()) &&
                (_animation_object->_stop_frame.stokes() == _animation_object->_current_frame.stokes()))) {
            return;
        }
        curr_frame = _animation_object->_stop_frame;
    } else {
        curr_frame = _animation_object->_next_frame;
    }

    auto file_id(_animation_object->_file_id);
    if (_frames.count(file_id)) {
        const std::unique_ptr<Frame>& frame = _frames.at(file_id);

        try {
            std::string err_message;
            auto channel = curr_frame.channel();
            auto stokes = curr_frame.stokes();

            if ((_animation_object->_tbb_context).is_group_execution_cancelled()) {
                return;
            }

            bool channel_changed(channel != frame->CurrentChannel());
            bool stokes_changed(stokes != frame->CurrentStokes());

            _animation_object->_current_frame = curr_frame;

            if (frame->SetImageChannels(channel, stokes, err_message)) {
                // RESPONSE: updated image raster/histogram
                bool send_histogram(true);
                SendRasterImageData(file_id, send_histogram);
                // Send Contour data if required
                SendContourData(file_id);
                // RESPONSE: data for all regions; no histogram
                UpdateRegionData(file_id, !send_histogram, channel_changed, stokes_changed);
            } else {
                if (!err_message.empty()) {
                    SendLogEvent(err_message, {"animation"}, CARTA::ErrorSeverity::ERROR);
                }
            }
        } catch (std::out_of_range& range_error) {
            string error = fmt::format("File id {} closed", file_id);
            SendLogEvent(error, {"animation"}, CARTA::ErrorSeverity::DEBUG);
        }
    } else {
        string error = fmt::format("File id {} not found", file_id);
        SendLogEvent(error, {"animation"}, CARTA::ErrorSeverity::DEBUG);
    }
}

bool Session::ExecuteAnimationFrame() {
    CARTA::AnimationFrame curr_frame;
    bool recycle_task = true;

    if (!_animation_object->_file_open) {
        return false;
    }

    if (_animation_object->_waiting_flow_event) {
        return false;
    }

    if (_animation_object->_stop_called) {
        ExecuteAnimationFrameInner(true);
        return false;
    }

    auto wait_duration_ms = std::chrono::duration_cast<std::chrono::microseconds>(
        _animation_object->_t_last + _animation_object->_frame_interval - std::chrono::high_resolution_clock::now());

    if ((wait_duration_ms.count() < _animation_object->_wait_duration_ms) || _animation_object->_always_wait) {
        // Wait for time to execute next frame processing.
        std::this_thread::sleep_for(wait_duration_ms);

        if (_animation_object->_stop_called) {
            ExecuteAnimationFrameInner(true);
            return false;
        }

        curr_frame = _animation_object->_next_frame;
        ExecuteAnimationFrameInner(false);

        CARTA::AnimationFrame tmp_frame;
        CARTA::AnimationFrame delta_frame = _animation_object->_delta_frame;

        if (_animation_object->_going_forward) {
            tmp_frame.set_channel(curr_frame.channel() + delta_frame.channel());
            tmp_frame.set_stokes(curr_frame.stokes() + delta_frame.stokes());

            if ((tmp_frame.channel() > _animation_object->_last_frame.channel()) ||
                (tmp_frame.stokes() > _animation_object->_last_frame.stokes())) {
                if (_animation_object->_reverse_at_end) {
                    _animation_object->_going_forward = false;
                } else if (_animation_object->_looping) {
                    tmp_frame.set_channel(_animation_object->_first_frame.channel());
                    tmp_frame.set_stokes(_animation_object->_first_frame.stokes());
                    _animation_object->_next_frame = tmp_frame;
                } else {
                    recycle_task = false;
                }
            } else {
                _animation_object->_next_frame = tmp_frame;
            }
        } else { // going backwards;
            tmp_frame.set_channel(curr_frame.channel() - _animation_object->_delta_frame.channel());
            tmp_frame.set_stokes(curr_frame.stokes() - _animation_object->_delta_frame.stokes());

            if ((tmp_frame.channel() < _animation_object->_first_frame.channel()) ||
                (tmp_frame.stokes() < _animation_object->_first_frame.stokes())) {
                if (_animation_object->_reverse_at_end) {
                    _animation_object->_going_forward = true;
                } else if (_animation_object->_looping) {
                    tmp_frame.set_channel(_animation_object->_last_frame.channel());
                    tmp_frame.set_stokes(_animation_object->_last_frame.stokes());
                    _animation_object->_next_frame = tmp_frame;
                } else {
                    recycle_task = false;
                }
            } else {
                _animation_object->_next_frame = tmp_frame;
            }
        }
        _animation_object->_t_last = std::chrono::high_resolution_clock::now();
    }
    return recycle_task;
}

void Session::StopAnimation(int file_id, const CARTA::AnimationFrame& frame) {
    if (!_animation_object) {
        return;
    }

    if (_animation_object->_file_id != file_id) {
        std::fprintf(stderr,
            "%p Session::StopAnimation called with file id %d."
            "Expected file id %d",
            this, file_id, _animation_object->_file_id);
        return;
    }

    _animation_object->_stop_frame = frame;
    _animation_object->_stop_called = true;
}

int Session::CalculateAnimationFlowWindow() {
    int gap;

    if (_animation_object->_going_forward) {
        if (_animation_object->_delta_frame.channel()) {
            gap = _animation_object->_current_frame.channel() - (_animation_object->_last_flow_frame).channel();
        } else {
            gap = _animation_object->_current_frame.stokes() - (_animation_object->_last_flow_frame).stokes();
        }
    } else { // going in reverse.
        if (_animation_object->_delta_frame.channel()) {
            gap = (_animation_object->_last_flow_frame).channel() - _animation_object->_current_frame.channel();
        } else {
            gap = (_animation_object->_last_flow_frame).stokes() - _animation_object->_delta_frame.stokes();
        }
    }

    return gap;
}

void Session::HandleAnimationFlowControlEvt(CARTA::AnimationFlowControl& message) {
    int gap;

    _animation_object->_last_flow_frame = message.received_frame();

    gap = CalculateAnimationFlowWindow();

    if (_animation_object->_waiting_flow_event) {
        if (gap <= CurrentFlowWindowSize()) {
            _animation_object->_waiting_flow_event = false;
            OnMessageTask* tsk = new (tbb::task::allocate_root(_animation_context)) AnimationTask(this);
            tbb::task::enqueue(*tsk);
        }
    }
}

void Session::CheckCancelAnimationOnFileClose(int file_id) {
    if (!_animation_object) {
        return;
    }
    _animation_object->_file_open = false;
    _animation_object->CancelExecution();
}

void Session::CancelExistingAnimation() {
    if (_animation_object) {
        _animation_object->CancelExecution();
        _animation_object = nullptr;
    }
}

void Session::DeleteFrame(int file_id) {
    std::unique_lock<std::mutex> lock(_frame_mutex);
    if (file_id == ALL_FILES) {
        for (auto& frame : _frames) {
            frame.second->DisconnectCalled(); // call to stop Frame's jobs and wait for jobs finished
            frame.second.reset();             // delete Frame
        }
        _frames.clear();
    } else if (_frames.count(file_id)) {
        _frames[file_id]->DisconnectCalled(); // call to stop Frame's jobs and wait for jobs finished
        _frames[file_id].reset();
        _frames.erase(file_id);
    }
}
