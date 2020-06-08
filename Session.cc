#include "Session.h"

#include <omp.h>
#include <signal.h>
#include <algorithm>
#include <chrono>
#include <limits>
#include <memory>
#include <thread>
#include <tuple>
#include <vector>

#include <casacore/casa/OS/File.h>
#include <fmt/format.h>
#include <tbb/parallel_for.h>
#include <tbb/task_group.h>
#include <xmmintrin.h>
#include <zstd.h>

#include <carta-protobuf/contour_image.pb.h>
#include <carta-protobuf/defs.pb.h>
#include <carta-protobuf/error.pb.h>
#include <carta-protobuf/raster_tile.pb.h>

#include "Carta.h"
#include "DataStream/Compression.h"
#include "EventHeader.h"
#include "FileList/FileExtInfoLoader.h"
#include "FileList/FileInfoLoader.h"
#include "InterfaceConstants.h"
#include "OnMessageTask.h"

#include "DBConnect.h"
#include "Util.h"

#define DEBUG(_DB_TEXT_) \
    {}

int Session::_num_sessions = 0;
int Session::_exit_after_num_seconds = 5;
bool Session::_exit_when_all_sessions_closed = false;

// Default constructor. Associates a websocket with a UUID and sets the root folder for all files
Session::Session(uWS::WebSocket<uWS::SERVER>* ws, uint32_t id, std::string root, std::string base, uS::Async* outgoing_async,
    FileListHandler* file_list_handler, bool verbose)
    : _socket(ws),
      _id(id),
      _root_folder(root),
      _base_folder(base),
      _table_controller(std::make_unique<carta::TableController>(_root_folder, _base_folder)),
      _verbose_logging(verbose),
      _loader(nullptr),
      _region_handler(nullptr),
      _outgoing_async(outgoing_async),
      _file_list_handler(file_list_handler),
      _animation_id(0),
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
    _outgoing_async->close();

    --_num_sessions;
    DEBUG(std::cout << this << " ~Session " << _num_sessions << std::endl;)
    if (!_num_sessions) {
        std::cout << "No remaining sessions." << std::endl;
        if (_exit_when_all_sessions_closed) {
            if (_exit_after_num_seconds == 0) {
                std::cout << "Exiting due to no sessions remaining" << std::endl;
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

void Session::ConnectCalled() {
    _connected = true;
    _base_context.reset();
    _histogram_context.reset();
    if (_animation_object) {
        _animation_object->ResetContext();
    }
}

// ********************************************************************************
// File browser

bool Session::FillExtendedFileInfo(CARTA::FileInfoExtended& extended_info, CARTA::FileInfo& file_info, const string& folder,
    const string& filename, string hdu, string& message) {
    // fill CARTA::FileInfoResponse submessages CARTA::FileInfo and CARTA::FileInfoExtended
    bool ext_file_info_ok(true);
    try {
        file_info.set_name(filename);
        casacore::String full_name(GetResolvedFilename(_root_folder, folder, filename));
        if (!full_name.empty()) {
            try {
                FileInfoLoader info_loader = FileInfoLoader(full_name);
                if (!info_loader.FillFileInfo(file_info)) {
                    return false;
                }
                if (hdu.empty()) { // use first when required
                    hdu = file_info.hdu_list(0);
                }

                _loader.reset(carta::FileLoader::GetLoader(full_name));

                FileExtInfoLoader ext_info_loader = FileExtInfoLoader(_loader.get());
                ext_file_info_ok = ext_info_loader.FillFileExtInfo(extended_info, filename, hdu, message);
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
        status = fmt::format("Start a new frontend and assign it with session id {}", session_id);
    } else {
        type = CARTA::SessionType::RESUMED;
        if (session_id != _id) {
            _id = session_id;
            status = fmt::format("Start a new backend and assign it with session id {}", session_id);
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

    uint32_t feature_flags = CARTA::ServerFeatureFlags::REGION_WRITE_ACCESS;
#ifdef _AUTH_SERVER_
    feature_flags |= CARTA::ServerFeatureFlags::USER_LAYOUTS;
    feature_flags |= CARTA::ServerFeatureFlags::USER_PREFERENCES;

    if (!GetLayoutsFromDB(&ack_message)) {
        // Can log failure here
    }
    if (!GetPreferencesFromDB(&ack_message)) {
        // Can log failure here
    }

#endif
    ack_message.set_server_feature_flags(feature_flags);
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
    auto& file_info = *response.mutable_file_info();
    auto& file_info_extended = *response.mutable_file_info_extended();
    string message;

    casacore::String hdu(request.hdu());
    hdu = hdu.before(" "); // strip FITS extension name
    bool success = FillExtendedFileInfo(file_info_extended, file_info, request.directory(), request.file(), hdu, message);

    // complete response message
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

bool Session::OnOpenFile(const CARTA::OpenFile& message, uint32_t request_id, bool silent) {
    // Create Frame and send response message
    const auto& directory(message.directory());
    const auto& filename(message.file());
    auto file_id(message.file_id());
    casacore::String hdu(message.hdu());
    hdu = hdu.before(" "); // strip FITS extension name

    // response message:
    CARTA::OpenFileAck ack;
    ack.set_file_id(file_id);
    string err_message;

    CARTA::FileInfo file_info;
    CARTA::FileInfoExtended file_info_extended;

    bool info_loaded = FillExtendedFileInfo(file_info_extended, file_info, directory, filename, hdu, err_message);

    bool success(false);

    if (info_loaded) {
        // Set hdu if empty
        if (hdu.empty()) { // use first
            hdu = file_info.hdu_list(0);
        }

        // create Frame for image
        auto frame = std::shared_ptr<Frame>(new Frame(_id, _loader.get(), hdu, _verbose_logging));
        _loader.release();

        if (frame->IsValid()) {
            // Check if the old _frames[file_id] object exists. If so, delete it.
            if (_frames.count(file_id) > 0) {
                DeleteFrame(file_id);
            }
            std::unique_lock<std::mutex> lock(_frame_mutex); // open/close lock
            _frames[file_id] = move(frame);
            lock.unlock();

            // copy file info, extended file info
            CARTA::FileInfo response_file_info = CARTA::FileInfo();
            response_file_info.set_name(file_info.name());
            response_file_info.set_type(file_info.type());
            response_file_info.set_size(file_info.size());
            response_file_info.add_hdu_list(hdu); // loaded hdu only
            *ack.mutable_file_info() = response_file_info;
            *ack.mutable_file_info_extended() = file_info_extended;
            uint32_t feature_flags = CARTA::FileFeatureFlags::FILE_FEATURE_NONE;
            // TODO: Determine these dynamically. For now, this is hard-coded for all HDF5 features.
            if (file_info.type() == CARTA::FileType::HDF5) {
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

    if (!silent) {
        ack.set_success(success);
        ack.set_message(err_message);
        SendEvent(CARTA::EventType::OPEN_FILE_ACK, request_id, ack);
    }

    if (success) {
        // send histogram with default requirements
        if (!SendRegionHistogramData(file_id, IMAGE_REGION_ID)) {
            std::string message = fmt::format("Image histogram for file id {} failed", file_id);
            SendLogEvent(message, {"open_file"}, CARTA::ErrorSeverity::ERROR);
        }
    }
    return success;
}

void Session::OnCloseFile(const CARTA::CloseFile& message) {
    CheckCancelAnimationOnFileClose(message.file_id());
    _file_settings.ClearSettings(message.file_id());
    DeleteFrame(message.file_id());
}

void Session::DeleteFrame(int file_id) {
    // call destructor and erase from map
    std::unique_lock<std::mutex> lock(_frame_mutex);
    if (file_id == ALL_FILES) {
        for (auto& frame : _frames) {
            frame.second->DisconnectCalled(); // call to stop Frame's jobs and wait for jobs finished
            frame.second.reset();             // delete Frame
        }
        _frames.clear();
        _image_channel_mutexes.clear();
        _image_channel_task_active.clear();
    } else if (_frames.count(file_id)) {
        _frames[file_id]->DisconnectCalled(); // call to stop Frame's jobs and wait for jobs finished
        _frames[file_id].reset();
        _frames.erase(file_id);
        _image_channel_mutexes.erase(file_id);
        _image_channel_task_active.erase(file_id);
    }
    if (_region_handler) {
        _region_handler->RemoveFrame(file_id);
    }
}

void Session::OnAddRequiredTiles(const CARTA::AddRequiredTiles& message, bool skip_data) {
    auto file_id = message.file_id();
    auto channel = _frames.at(file_id)->CurrentChannel();
    auto stokes = _frames.at(file_id)->CurrentStokes();
    auto animation_id = AnimationRunning() ? _animation_id : 0;
    if (!message.tiles().empty() && _frames.count(file_id)) {
        if (skip_data) {
            // Update view settings and skip sending data
            _frames.at(file_id)->SetAnimationViewSettings(message);
            return;
        }

        CARTA::RasterTileSync start_message;
        start_message.set_file_id(file_id);
        start_message.set_channel(channel);
        start_message.set_stokes(stokes);
        start_message.set_animation_id(animation_id);
        start_message.set_end_sync(false);
        start_message.set_animation_id(animation_id);
        SendFileEvent(file_id, CARTA::EventType::RASTER_TILE_SYNC, 0, start_message);

        int n = message.tiles_size();
        CARTA::CompressionType compression_type = message.compression_type();
        float compression_quality = message.compression_quality();
        int stride = std::min(n, std::min(CARTA::global_thread_count, CARTA::MAX_TILING_TASKS));

        auto lambda = [&](int start) {
            for (int i = start; i < n; i += stride) {
                const auto& encoded_coordinate = message.tiles(i);
                CARTA::RasterTileData raster_tile_data;
                raster_tile_data.set_file_id(file_id);
                raster_tile_data.set_animation_id(animation_id);
                auto tile = Tile::Decode(encoded_coordinate);
                if (_frames.at(file_id)->FillRasterTileData(
                        raster_tile_data, tile, channel, stokes, compression_type, compression_quality)) {
                    SendFileEvent(file_id, CARTA::EventType::RASTER_TILE_DATA, 0, raster_tile_data);
                } else {
                    fmt::print("Problem getting tile layer={}, x={}, y={}\n", tile.layer, tile.x, tile.y);
                }
            }
        };

        auto t_start_get_tile_data = std::chrono::high_resolution_clock::now();

#pragma omp parallel for
        for (int j = 0; j < stride; j++) {
            lambda(j);
        }

        if (_verbose_logging) {
            // Measure duration for get tile data
            auto t_end_get_tile_data = std::chrono::high_resolution_clock::now();
            auto dt_get_tile_data =
                std::chrono::duration_cast<std::chrono::milliseconds>(t_end_get_tile_data - t_start_get_tile_data).count();
            fmt::print("Get tile data group in {} ms\n", dt_get_tile_data);
        }

        // Send final message with no tiles to signify end of the tile stream, for synchronisation purposes
        CARTA::RasterTileSync final_message;
        final_message.set_file_id(file_id);
        final_message.set_channel(channel);
        final_message.set_stokes(stokes);
        final_message.set_animation_id(animation_id);
        final_message.set_end_sync(true);
        final_message.set_animation_id(animation_id);
        SendFileEvent(file_id, CARTA::EventType::RASTER_TILE_SYNC, 0, final_message);
    }
}

void Session::OnSetImageChannels(const CARTA::SetImageChannels& message) {
    auto file_id(message.file_id());
    if (_frames.count(file_id)) {
        auto frame = _frames.at(file_id);
        std::string err_message;
        auto channel_target = message.channel();
        auto stokes_target = message.stokes();
        bool channel_changed(channel_target != frame->CurrentChannel());
        bool stokes_changed(stokes_target != frame->CurrentStokes());
        if (frame->SetImageChannels(channel_target, stokes_target, err_message)) {
            // Send Contour data if required
            SendContourData(file_id);
            bool send_histogram(true);
            UpdateImageData(file_id, send_histogram, channel_changed, stokes_changed);
            UpdateRegionData(file_id, ALL_REGIONS, channel_changed, stokes_changed);
        } else {
            if (!err_message.empty()) {
                SendLogEvent(err_message, {"channels"}, CARTA::ErrorSeverity::ERROR);
            }
        }

        // Send any required tiles if they have been requested
        if (message.has_required_tiles()) {
            OnAddRequiredTiles(message.required_tiles());
        }
    } else {
        string error = fmt::format("File id {} not found", file_id);
        SendLogEvent(error, {"channels"}, CARTA::ErrorSeverity::DEBUG);
    }
}

void Session::OnSetCursor(const CARTA::SetCursor& message, uint32_t request_id) {
    // Set cursor for image indicated by file_id
    auto file_id(message.file_id());
    if (_frames.count(file_id)) { // reference Frame for Region exists
        if (message.has_spatial_requirements()) {
            auto requirements = message.spatial_requirements();
            _frames.at(file_id)->SetSpatialRequirements(requirements.region_id(),
                std::vector<std::string>(requirements.spatial_profiles().begin(), requirements.spatial_profiles().end()));
        }
        if (_frames.at(file_id)->SetCursor(message.point().x(), message.point().y())) { // cursor changed
            SendSpatialProfileData(file_id, CURSOR_REGION_ID);
            SendSpectralProfileData(file_id, CURSOR_REGION_ID);
        }
    } else {
        string error = fmt::format("File id {} not found", file_id);
        SendLogEvent(error, {"cursor"}, CARTA::ErrorSeverity::DEBUG);
    }
}

bool Session::OnSetRegion(const CARTA::SetRegion& message, uint32_t request_id, bool silent) {
    // Create new Region or update existing Region
    auto file_id(message.file_id());
    auto region_id(message.region_id());
    std::string err_message;
    bool success(false);

    if (_frames.count(file_id)) { // reference Frame for Region exists
        casacore::CoordinateSystem* csys = _frames.at(file_id)->CoordinateSystem();

        if (!_region_handler) { // created on demand only
            _region_handler = std::unique_ptr<carta::RegionHandler>(new carta::RegionHandler(_verbose_logging));
        }
        std::vector<CARTA::Point> points = {message.control_points().begin(), message.control_points().end()};
        success =
            _region_handler->SetRegion(region_id, file_id, message.region_name(), message.region_type(), points, message.rotation(), csys);

        // log error
        if (!success) {
            err_message = fmt::format("Region {} parameters for file {} failed", region_id, file_id);
            SendLogEvent(err_message, {"region"}, CARTA::ErrorSeverity::DEBUG);
        }
    } else {
        err_message = fmt::format("Cannot set region, file id {} not found", file_id);
    }

    // RESPONSE
    if (!silent) {
        CARTA::SetRegionAck ack;
        ack.set_region_id(region_id);
        ack.set_success(success);
        ack.set_message(err_message);
        SendEvent(CARTA::EventType::SET_REGION_ACK, request_id, ack);
    }

    // update data streams if requirements set and region changed
    if (success && _region_handler->RegionChanged(region_id)) {
        OnMessageTask* tsk = new (tbb::task::allocate_root(this->Context())) RegionDataStreamsTask(this, ALL_FILES, region_id);
        tbb::task::enqueue(*tsk);
    }

    return success;
}

void Session::OnRemoveRegion(const CARTA::RemoveRegion& message) {
    if (_region_handler) {
        _region_handler->RemoveRegion(message.region_id());
    }
}

void Session::OnImportRegion(const CARTA::ImportRegion& message, uint32_t request_id) {
    auto file_id(message.group_id()); // eventually, import into wcs group
    if (_frames.count(file_id)) {
        CARTA::FileType file_type(message.type());
        std::string directory(message.directory()), filename(message.file());
        std::vector<std::string> contents = {message.contents().begin(), message.contents().end()};
        CARTA::ImportRegionAck import_ack; // response

        // check for file or contents set
        bool import_file(!directory.empty() && !filename.empty()), import_contents(!contents.empty());
        if (!import_file && !import_contents) {
            import_ack.set_success(false);
            import_ack.set_message("Import region failed: cannot import by filename or contents.");
            import_ack.add_regions();
            SendFileEvent(file_id, CARTA::EventType::IMPORT_REGION_ACK, request_id, import_ack);
            return;
        }

        std::string region_file; // name or contents
        if (import_file) {
            // check that file can be opened
            region_file = GetResolvedFilename(_root_folder, directory, filename);
            casacore::File ccfile(region_file);
            if (!ccfile.exists() || !ccfile.isReadable()) {
                import_ack.set_success(false);
                import_ack.set_message("Import region failed: cannot open file.");
                import_ack.add_regions();
                SendFileEvent(file_id, CARTA::EventType::IMPORT_REGION_ACK, request_id, import_ack);
                return;
            }
        } else {
            // combine vector into one string
            for (auto& line : contents) {
                region_file.append(line);
            }
        }

        if (!_region_handler) { // created on demand only
            _region_handler = std::unique_ptr<carta::RegionHandler>(new carta::RegionHandler(_verbose_logging));
        }

        _region_handler->ImportRegion(file_id, _frames.at(file_id), file_type, region_file, import_file, import_ack);

        // send any errors to log
        std::string ack_message(import_ack.message());
        if (!ack_message.empty()) {
            CARTA::ErrorSeverity level = (import_ack.success() ? CARTA::ErrorSeverity::WARNING : CARTA::ErrorSeverity::ERROR);
            SendLogEvent(ack_message, {"import"}, level);
        }
        // send ack message
        SendFileEvent(file_id, CARTA::EventType::IMPORT_REGION_ACK, request_id, import_ack);
    } else {
        std::string error = fmt::format("File id {} not found", file_id);
        SendLogEvent(error, {"import"}, CARTA::ErrorSeverity::DEBUG);
    }
}

void Session::OnExportRegion(const CARTA::ExportRegion& message, uint32_t request_id) {
    auto file_id(message.file_id());
    if (_frames.count(file_id)) {
        if (!_region_handler) {
            std::string error = fmt::format("No region handler for export");
            SendLogEvent(error, {"export"}, CARTA::ErrorSeverity::ERROR);
            return;
        }

        // Export filename (optional, for server-side export)
        std::string directory(message.directory()), filename(message.file());
        std::string abs_filename;
        if (!directory.empty() && !filename.empty()) {
            // export file is on server, form path with filename
            casacore::Path root_path(_root_folder);
            root_path.append(directory);
            root_path.append(filename);
            abs_filename = root_path.absoluteName();
        }

        std::vector<int> region_ids = {message.region_id().begin(), message.region_id().end()};
        CARTA::ExportRegionAck export_ack;
        _region_handler->ExportRegion(
            file_id, _frames.at(file_id), message.type(), message.coord_type(), region_ids, abs_filename, export_ack);
        SendFileEvent(file_id, CARTA::EventType::EXPORT_REGION_ACK, request_id, export_ack);
    } else {
        string error = fmt::format("File id {} not found", file_id);
        SendLogEvent(error, {"export"}, CARTA::ErrorSeverity::DEBUG);
    }
}

void Session::OnSetSpatialRequirements(const CARTA::SetSpatialRequirements& message) {
    auto file_id(message.file_id());
    if (_frames.count(file_id)) {
        auto region_id = message.region_id();
        if (region_id > CURSOR_REGION_ID) {
            string error = fmt::format("Spatial requirements not valid for non-cursor region ", region_id);
            SendLogEvent(error, {"spatial"}, CARTA::ErrorSeverity::ERROR);
        } else {
            if (_frames.at(file_id)->SetSpatialRequirements(
                    region_id, std::vector<std::string>(message.spatial_profiles().begin(), message.spatial_profiles().end()))) {
                SendSpatialProfileData(file_id, region_id);
            } else {
                string error = fmt::format("Spatial profiles not valid for region id {}", region_id);
                SendLogEvent(error, {"spatial"}, CARTA::ErrorSeverity::ERROR);
            }
        }
    } else {
        string error = fmt::format("File id {} not found", file_id);
        SendLogEvent(error, {"spatial"}, CARTA::ErrorSeverity::DEBUG);
    }
}

void Session::OnSetHistogramRequirements(const CARTA::SetHistogramRequirements& message, uint32_t request_id) {
    auto file_id(message.file_id());
    auto region_id = message.region_id();
    bool requirements_set(false);

    if (_frames.count(file_id)) {
        // Catch cube histogram cancel here
        if ((region_id == CUBE_REGION_ID) && (message.histograms_size() == 0)) { // cancel!
            _histogram_progress = HISTOGRAM_CANCEL;
            _histogram_context.cancel_group_execution();
            SendLogEvent("Histogram cancelled", {"histogram"}, CARTA::ErrorSeverity::INFO);
            return;
        }

        std::vector<CARTA::SetHistogramRequirements_HistogramConfig> requirements = {
            message.histograms().begin(), message.histograms().end()};

        if (region_id > CURSOR_REGION_ID) {
            if (!_region_handler) {
                string error = fmt::format("Region {} has not been set", region_id);
                SendLogEvent(error, {"histogram"}, CARTA::ErrorSeverity::ERROR);
                return;
            }
            requirements_set = _region_handler->SetHistogramRequirements(region_id, file_id, _frames.at(file_id), requirements);
        } else {
            requirements_set = _frames.at(file_id)->SetHistogramRequirements(region_id, requirements);
        }

        if (requirements_set) {
            if ((message.histograms_size() > 0) && !SendRegionHistogramData(file_id, region_id)) {
                std::string message = fmt::format("Histogram calculation for region {} failed", region_id);
                SendLogEvent(message, {"histogram"}, CARTA::ErrorSeverity::WARNING);
            }
        } else {
            std::string error = fmt::format("Histogram requirements not valid for region id {}", region_id);
            SendLogEvent(error, {"histogram"}, CARTA::ErrorSeverity::ERROR);
        }
    } else {
        string error = fmt::format("File id {} not found", file_id);
        SendLogEvent(error, {"histogram"}, CARTA::ErrorSeverity::DEBUG);
        return;
    }
}

void Session::OnSetSpectralRequirements(const CARTA::SetSpectralRequirements& message) {
    auto file_id(message.file_id());
    auto region_id = message.region_id();
    bool requirements_set(false);

    if (_frames.count(file_id)) {
        if (_frames.at(file_id)->ImageShape().size() < 3) {
            string error = "Spectral profile not valid for 2D image.";
            SendLogEvent(error, {"spectral"}, CARTA::ErrorSeverity::WARNING);
            return;
        }

        std::vector<CARTA::SetSpectralRequirements_SpectralConfig> requirements = {
            message.spectral_profiles().begin(), message.spectral_profiles().end()};

        if (region_id > CURSOR_REGION_ID) {
            if (!_region_handler) {
                string error = fmt::format("Region {} has not been set", region_id);
                SendLogEvent(error, {"spectral"}, CARTA::ErrorSeverity::ERROR);
                return;
            }
            requirements_set = _region_handler->SetSpectralRequirements(region_id, file_id, _frames.at(file_id), requirements);
        } else {
            requirements_set = _frames.at(file_id)->SetSpectralRequirements(region_id, requirements);
        }

        if (requirements_set) {
            // RESPONSE
            OnMessageTask* tsk = new (tbb::task::allocate_root(this->Context())) SpectralProfileTask(this, file_id, region_id);
            tbb::task::enqueue(*tsk);
        } else if (region_id != IMAGE_REGION_ID) { // not sure why frontend sends this
            string error = fmt::format("Spectral requirements not valid for region id {}", region_id);
            SendLogEvent(error, {"spectral"}, CARTA::ErrorSeverity::ERROR);
        }
    } else {
        string error = fmt::format("File id {} not found", file_id);
        SendLogEvent(error, {"spectral"}, CARTA::ErrorSeverity::DEBUG);
    }
}

void Session::OnSetStatsRequirements(const CARTA::SetStatsRequirements& message) {
    auto file_id(message.file_id());
    auto region_id = message.region_id();
    bool requirements_set(false);

    if (_frames.count(file_id)) {
        std::vector<CARTA::StatsType> requirements;
        for (size_t i = 0; i < message.stats_size(); ++i) {
            requirements.push_back(message.stats(i));
        }

        if (region_id > CURSOR_REGION_ID) {
            if (!_region_handler) {
                string error = fmt::format("Region {} has not been set", region_id);
                SendLogEvent(error, {"stats"}, CARTA::ErrorSeverity::ERROR);
                return;
            }
            requirements_set = _region_handler->SetStatsRequirements(region_id, file_id, _frames.at(file_id), requirements);
        } else {
            requirements_set = _frames.at(file_id)->SetStatsRequirements(region_id, requirements);
        }

        if (requirements_set) {
            if ((message.stats_size() > 0) && !SendRegionStatsData(file_id, region_id)) {
                std::string error = fmt::format("Statistics calculation for region {} failed", region_id);
                SendLogEvent(error, {"stats"}, CARTA::ErrorSeverity::ERROR);
            }
        } else {
            string error = fmt::format("Stats requirements not valid for region id {}", region_id);
            SendLogEvent(error, {"stats"}, CARTA::ErrorSeverity::ERROR);
        }
    } else {
        string error = fmt::format("File id {} not found", file_id);
        SendLogEvent(error, {"stats"}, CARTA::ErrorSeverity::DEBUG);
    }
}

void Session::OnSetUserPreferences(const CARTA::SetUserPreferences& request, uint32_t request_id) {
    CARTA::SetUserPreferencesAck ack_message;
    bool result;

#ifdef _AUTH_SERVER_
    result = SaveUserPreferencesToDB(request);
#endif

    ack_message.set_success(result);

    SendEvent(CARTA::EventType::SET_USER_PREFERENCES_ACK, request_id, ack_message);
}

void Session::OnSetUserLayout(const CARTA::SetUserLayout& request, uint32_t request_id) {
    CARTA::SetUserLayoutAck ack_message;

#ifdef _AUTH_SERVER_
    if (SaveLayoutToDB(request.name(), request.value())) {
        ack_message.set_success(true);
    } else {
        ack_message.set_success(false);
    }
#else
    ack_message.set_success(false);
#endif

    SendEvent(CARTA::EventType::SET_USER_LAYOUT_ACK, request_id, ack_message);
}

void Session::OnSetContourParameters(const CARTA::SetContourParameters& message) {
    if (_frames.count(message.file_id())) {
        const int num_levels = message.levels_size();
        if (_frames.at(message.file_id())->SetContourParameters(message) && num_levels) {
            SendContourData(message.file_id());
        }
    }
}

void Session::OnResumeSession(const CARTA::ResumeSession& message, uint32_t request_id) {
    bool success(true);
    // Error message
    std::string err_message;
    std::string err_file_ids = "Problem loading files: ";
    std::string err_region_ids = "Problem loading regions: ";

    // Stop the streaming spectral profile, cube histogram and animation processes
    DisconnectCalled();

    // Clear the message queue
    _out_msgs.clear();

    // Reconnect the session
    ConnectCalled();

    // Close all images
    CARTA::CloseFile close_file_msg;
    close_file_msg.set_file_id(-1);
    OnCloseFile(close_file_msg);

    auto t_start_resume = std::chrono::high_resolution_clock::now();

    // Open images
    for (int i = 0; i < message.images_size(); ++i) {
        const CARTA::ImageProperties& image = message.images(i);
        int file_id(image.file_id());

        CARTA::OpenFile open_file_msg;
        open_file_msg.set_directory(image.directory());
        open_file_msg.set_file(image.file());
        open_file_msg.set_hdu(image.hdu());
        open_file_msg.set_file_id(file_id);
        bool file_ok(true);
        if (!OnOpenFile(open_file_msg, request_id, true)) {
            success = false;
            file_ok = false;
            // Error message
            std::string file_id_str = std::to_string(file_id) + " ";
            err_file_ids.append(file_id_str);
        }

        if (file_ok) {
            // Set image channels
            CARTA::SetImageChannels set_image_channels_msg;
            set_image_channels_msg.set_file_id(file_id);
            set_image_channels_msg.set_channel(image.channel());
            set_image_channels_msg.set_stokes(image.stokes());
            OnSetImageChannels(set_image_channels_msg);

            // Set regions
            for (int j = 0; j < image.regions_size(); ++j) {
                const CARTA::RegionProperties& region = image.regions(j);
                CARTA::SetRegion set_region_msg;
                set_region_msg.set_file_id(file_id);
                set_region_msg.set_region_id(region.region_id());
                set_region_msg.set_region_name(region.region_info().region_name());
                set_region_msg.set_rotation(region.region_info().rotation());
                set_region_msg.set_region_type(region.region_info().region_type());
                *set_region_msg.mutable_control_points() = {
                    region.region_info().control_points().begin(), region.region_info().control_points().end()};
                if (!OnSetRegion(set_region_msg, request_id, true)) {
                    success = false;
                    // Error message
                    std::string region_id = std::to_string(region.region_id()) + " ";
                    err_region_ids.append(region_id);
                }
            }
        }
    }
    // Measure duration for resume
    if (_verbose_logging) {
        auto t_end_resume = std::chrono::high_resolution_clock::now();
        auto dt_resume = std::chrono::duration_cast<std::chrono::microseconds>(t_end_resume - t_start_resume).count();
        fmt::print("Resume in {} ms\n", dt_resume * 1e-3);
    }

    if (_verbose_logging) {
        auto t_end_resume = std::chrono::high_resolution_clock::now();
        auto dt_resume = std::chrono::duration_cast<std::chrono::milliseconds>(t_end_resume - t_start_resume).count();
        fmt::print("Resume in {} ms\n", dt_resume);
    }

    // RESPONSE
    CARTA::ResumeSessionAck ack;
    ack.set_success(success);
    if (!success) {
        err_message = err_file_ids + err_region_ids;
        ack.set_message(err_message);
    }
    SendEvent(CARTA::EventType::RESUME_SESSION_ACK, request_id, ack);
}

void Session::OnCatalogFileList(CARTA::CatalogListRequest file_list_request, uint32_t request_id) {
    CARTA::CatalogListResponse file_list_response;
    _table_controller->OnFileListRequest(file_list_request, file_list_response);
    SendEvent(CARTA::EventType::CATALOG_LIST_RESPONSE, request_id, file_list_response);
}

void Session::OnCatalogFileInfo(CARTA::CatalogFileInfoRequest file_info_request, uint32_t request_id) {
    CARTA::CatalogFileInfoResponse file_info_response;
    _table_controller->OnFileInfoRequest(file_info_request, file_info_response);
    SendEvent(CARTA::EventType::CATALOG_FILE_INFO_RESPONSE, request_id, file_info_response);
}

void Session::OnOpenCatalogFile(CARTA::OpenCatalogFile open_file_request, uint32_t request_id) {
    CARTA::OpenCatalogFileAck open_file_response;
    _table_controller->OnOpenFileRequest(open_file_request, open_file_response);
    SendEvent(CARTA::EventType::OPEN_CATALOG_FILE_ACK, request_id, open_file_response);
}

void Session::OnCloseCatalogFile(CARTA::CloseCatalogFile close_file_request) {
    _table_controller->OnCloseFileRequest(close_file_request);
}

void Session::OnCatalogFilter(CARTA::CatalogFilterRequest filter_request, uint32_t request_id) {
    _table_controller->OnFilterRequest(filter_request, [&](const CARTA::CatalogFilterResponse& filter_response) {
        // Send partial or final results
        SendEvent(CARTA::EventType::CATALOG_FILTER_RESPONSE, request_id, filter_response, true);
    });
}

// ******** SEND DATA STREAMS *********

bool Session::CalculateCubeHistogram(int file_id, CARTA::RegionHistogramData& cube_histogram_message) {
    // Calculate cube histogram message fields, and set cache in Frame
    // First try Frame::FillRegionHistogramData to get cached histogram before calculating it here.
    bool calculated(false);
    if (_frames.count(file_id)) {
        try {
            int stokes(_frames.at(file_id)->CurrentStokes());
            HistogramConfig cube_histogram_config;
            if (!_frames.at(file_id)->GetCubeHistogramConfig(cube_histogram_config)) {
                return calculated; // no requirements
            }

            auto t_start_cube_histogram = std::chrono::high_resolution_clock::now();

            auto channel = cube_histogram_config.channel;
            auto num_bins = cube_histogram_config.num_bins;

            // To send periodic updates
            _histogram_progress = HISTOGRAM_START;
            auto t_start = std::chrono::high_resolution_clock::now();
            int request_id(0);
            size_t num_channels(_frames.at(file_id)->NumChannels());
            size_t total_channels(num_channels * 2); // for progress; go through chans twice, for stats then histogram

            // stats for entire cube
            carta::BasicStats<float> cube_stats;
            for (size_t chan = 0; chan < num_channels; ++chan) {
                // stats for this channel
                carta::BasicStats<float> channel_stats;
                if (!_frames.at(file_id)->GetBasicStats(chan, stokes, channel_stats)) {
                    return calculated;
                }
                cube_stats.join(channel_stats);

                // check for cancel
                if (_histogram_context.is_group_execution_cancelled()) {
                    break;
                }

                // check for progress update
                auto t_end = std::chrono::high_resolution_clock::now();
                auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();
                if ((dt / 1e3) > 2.0) {
                    // send progress
                    float this_chan(chan);
                    float progress = this_chan / total_channels;
                    CARTA::RegionHistogramData progress_msg;
                    CreateCubeHistogramMessage(progress_msg, file_id, stokes, progress);
                    auto message_histogram = progress_msg.add_histograms();
                    SendFileEvent(file_id, CARTA::EventType::REGION_HISTOGRAM_DATA, request_id, progress_msg);
                    t_start = t_end;
                }
            }

            // check cancel and proceed
            if (!_histogram_context.is_group_execution_cancelled()) {
                _frames.at(file_id)->CacheCubeStats(stokes, cube_stats);

                // send progress message: half done
                float progress = 0.50;
                CARTA::RegionHistogramData half_progress;
                CreateCubeHistogramMessage(half_progress, file_id, stokes, progress);
                auto message_histogram = half_progress.add_histograms();
                SendFileEvent(file_id, CARTA::EventType::REGION_HISTOGRAM_DATA, request_id, half_progress);

                // get histogram bins for each channel and accumulate bin counts in cube_bins
                std::vector<int> cube_bins;
                carta::HistogramResults chan_histogram; // histogram for each channel using cube stats
                for (size_t chan = 0; chan < num_channels; ++chan) {
                    if (!_frames.at(file_id)->CalculateHistogram(CUBE_REGION_ID, chan, stokes, num_bins, cube_stats, chan_histogram)) {
                        return calculated; // channel histogram failed
                    }

                    // add channel bins to cube bins
                    if (chan == 0) {
                        cube_bins = {chan_histogram.histogram_bins.begin(), chan_histogram.histogram_bins.end()};
                    } else { // add chan histogram bins to cube histogram bins
                        std::transform(chan_histogram.histogram_bins.begin(), chan_histogram.histogram_bins.end(), cube_bins.begin(),
                            cube_bins.begin(), std::plus<int>());
                    }

                    // check for cancel
                    if (_histogram_context.is_group_execution_cancelled()) {
                        break;
                    }

                    auto t_end = std::chrono::high_resolution_clock::now();
                    auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();
                    if ((dt / 1e3) > 2.0) {
                        // Send progress update
                        float this_chan(chan);
                        progress = 0.5 + (this_chan / total_channels);
                        CARTA::RegionHistogramData progress_msg;
                        CreateCubeHistogramMessage(progress_msg, file_id, stokes, progress);
                        auto message_histogram = progress_msg.add_histograms();
                        message_histogram->set_channel(ALL_CHANNELS);
                        message_histogram->set_num_bins(chan_histogram.num_bins);
                        message_histogram->set_bin_width(chan_histogram.bin_width);
                        message_histogram->set_first_bin_center(chan_histogram.bin_center);
                        message_histogram->set_mean(cube_stats.mean);
                        message_histogram->set_std_dev(cube_stats.stdDev);
                        *message_histogram->mutable_bins() = {cube_bins.begin(), cube_bins.end()};
                        SendFileEvent(file_id, CARTA::EventType::REGION_HISTOGRAM_DATA, request_id, progress_msg);
                        t_start = t_end;
                    }
                }

                // set completed cube histogram
                if (!_histogram_context.is_group_execution_cancelled()) {
                    cube_histogram_message.set_file_id(file_id);
                    cube_histogram_message.set_region_id(CUBE_REGION_ID);
                    cube_histogram_message.set_stokes(stokes);
                    cube_histogram_message.set_progress(HISTOGRAM_COMPLETE);
                    // fill histogram fields from last channel histogram
                    cube_histogram_message.clear_histograms();
                    auto message_histogram = cube_histogram_message.add_histograms();
                    message_histogram->set_channel(ALL_CHANNELS);
                    message_histogram->set_num_bins(chan_histogram.num_bins);
                    message_histogram->set_bin_width(chan_histogram.bin_width);
                    message_histogram->set_first_bin_center(chan_histogram.bin_center);
                    message_histogram->set_mean(cube_stats.mean);
                    message_histogram->set_std_dev(cube_stats.stdDev);
                    *message_histogram->mutable_bins() = {cube_bins.begin(), cube_bins.end()};

                    // cache cube histogram
                    carta::HistogramResults cube_results;
                    cube_results.num_bins = chan_histogram.num_bins;
                    cube_results.bin_width = chan_histogram.bin_width;
                    cube_results.bin_center = chan_histogram.bin_center;
                    cube_results.histogram_bins = {cube_bins.begin(), cube_bins.end()};
                    _frames.at(file_id)->CacheCubeHistogram(stokes, cube_results);

                    if (_verbose_logging) {
                        auto t_end_cube_histogram = std::chrono::high_resolution_clock::now();
                        auto dt_cube_histogram =
                            std::chrono::duration_cast<std::chrono::microseconds>(t_end_cube_histogram - t_start_cube_histogram).count();
                        fmt::print("Fill cube histogram in {} ms at {} MPix/s\n", dt_cube_histogram * 1e-3,
                            (float)cube_stats.num_pixels / dt_cube_histogram);
                    }

                    calculated = true;
                }
            }
            _histogram_progress = HISTOGRAM_COMPLETE;
        } catch (std::out_of_range& range_error) {
            _histogram_progress = HISTOGRAM_COMPLETE;
            string error = fmt::format("File id {} closed", file_id);
            SendLogEvent(error, {"histogram"}, CARTA::ErrorSeverity::DEBUG);
        }
    } else {
        string error = fmt::format("File id {} not found", file_id);
        SendLogEvent(error, {"histogram"}, CARTA::ErrorSeverity::DEBUG);
    }
    return calculated;
}

void Session::CreateCubeHistogramMessage(CARTA::RegionHistogramData& msg, int file_id, int stokes, float progress) {
    // make new message and update progress
    msg.set_file_id(file_id);
    msg.set_region_id(CUBE_REGION_ID);
    msg.set_stokes(stokes);
    msg.set_progress(progress);
    _histogram_progress = progress;
}

bool Session::SendSpatialProfileData(int file_id, int region_id) {
    // return true if data sent
    bool data_sent(false);
    if (region_id > CURSOR_REGION_ID) {
        string error = fmt::format("Spatial profiles not valid for non-cursor region ", region_id);
        SendLogEvent(error, {"spatial"}, CARTA::ErrorSeverity::DEBUG);
    } else if (region_id == CURSOR_REGION_ID) {
        // Cursor spatial profile
        if (_frames.count(file_id)) {
            CARTA::SpatialProfileData spatial_profile_data;
            if (_frames.at(file_id)->FillSpatialProfileData(region_id, spatial_profile_data)) {
                spatial_profile_data.set_file_id(file_id);
                spatial_profile_data.set_region_id(region_id);
                SendFileEvent(file_id, CARTA::EventType::SPATIAL_PROFILE_DATA, 0, spatial_profile_data);
                data_sent = true;
            }
        }
    }
    return data_sent;
}

bool Session::SendSpectralProfileData(int file_id, int region_id, bool stokes_changed) {
    // return true if data sent
    bool data_sent(false);
    if (region_id == ALL_REGIONS && !_region_handler) {
        return data_sent;
    }

    if ((region_id > CURSOR_REGION_ID) || (region_id == ALL_REGIONS) || (file_id == ALL_FILES)) {
        // Region spectral profile
        CARTA::SpectralProfileData profile_data;
        data_sent = _region_handler->FillSpectralProfileData(
            [&](CARTA::SpectralProfileData profile_data) {
                if (profile_data.profiles_size() > 0) {
                    // send (partial) profile data to the frontend for each region/file combo
                    SendFileEvent(profile_data.file_id(), CARTA::EventType::SPECTRAL_PROFILE_DATA, 0, profile_data);
                }
            },
            region_id, file_id, stokes_changed);
    } else if (region_id == CURSOR_REGION_ID) {
        // Cursor spectral profile
        if (_frames.count(file_id)) {
            CARTA::SpectralProfileData profile_data;
            data_sent = _frames.at(file_id)->FillSpectralProfileData(
                [&](CARTA::SpectralProfileData profile_data) {
                    if (profile_data.profiles_size() > 0) {
                        profile_data.set_file_id(file_id);
                        profile_data.set_region_id(region_id);
                        // send (partial) profile data to the frontend
                        SendFileEvent(file_id, CARTA::EventType::SPECTRAL_PROFILE_DATA, 0, profile_data);
                    }
                },
                region_id, stokes_changed);
        }
    }
    return data_sent;
}

bool Session::SendRegionHistogramData(int file_id, int region_id) {
    // return true if data sent
    bool data_sent(false);
    if (region_id == ALL_REGIONS && !_region_handler) {
        return data_sent;
    }

    if ((region_id > CURSOR_REGION_ID) || (region_id == ALL_REGIONS) || (file_id == ALL_FILES)) {
        // Region histogram
        CARTA::RegionHistogramData histogram_data;
        data_sent = _region_handler->FillRegionHistogramData(
            [&](CARTA::RegionHistogramData histogram_data) {
                if (histogram_data.histograms_size() > 0) {
                    SendFileEvent(histogram_data.file_id(), CARTA::EventType::REGION_HISTOGRAM_DATA, 0, histogram_data);
                }
            },
            region_id, file_id);
    } else if (region_id < CURSOR_REGION_ID) {
        // Image or cube histogram
        if (_frames.count(file_id)) {
            CARTA::RegionHistogramData histogram_data;
            histogram_data.set_file_id(file_id);
            histogram_data.set_region_id(region_id);
            if (_frames.at(file_id)->FillRegionHistogramData(region_id, histogram_data)) {
                SendFileEvent(file_id, CARTA::EventType::REGION_HISTOGRAM_DATA, 0, histogram_data);
                data_sent = true;
            } else if (region_id == CUBE_REGION_ID) { // not in cache, calculate cube histogram
                if (CalculateCubeHistogram(file_id, histogram_data)) {
                    SendFileEvent(file_id, CARTA::EventType::REGION_HISTOGRAM_DATA, 0, histogram_data);
                    data_sent = true;
                }
            }
        }
    } else {
        string error = fmt::format("File id {} not found", file_id);
        SendLogEvent(error, {"histogram"}, CARTA::ErrorSeverity::DEBUG);
    }
    return data_sent;
}

bool Session::SendRegionStatsData(int file_id, int region_id) {
    // return true if data sent
    bool data_sent(false);
    if (region_id == ALL_REGIONS && !_region_handler) {
        return data_sent;
    }

    if ((region_id > CURSOR_REGION_ID) || (region_id == ALL_REGIONS) || (file_id == ALL_FILES)) {
        // Region stats
        CARTA::RegionStatsData stats_data;
        data_sent = _region_handler->FillRegionStatsData(
            [&](CARTA::RegionStatsData region_stats_data) {
                if (region_stats_data.statistics_size() > 0) {
                    SendFileEvent(region_stats_data.file_id(), CARTA::EventType::REGION_STATS_DATA, 0, region_stats_data);
                }
            },
            region_id, file_id);
    } else if (region_id == IMAGE_REGION_ID) {
        // Image stats
        if (_frames.count(file_id)) {
            CARTA::RegionStatsData region_stats_data;
            if (_frames.at(file_id)->FillRegionStatsData(region_id, region_stats_data)) {
                region_stats_data.set_file_id(file_id);
                region_stats_data.set_region_id(region_id);
                SendFileEvent(file_id, CARTA::EventType::REGION_STATS_DATA, 0, region_stats_data);
                data_sent = true;
            }
        }
    }
    return data_sent;
}

bool Session::SendContourData(int file_id) {
    if (_frames.count(file_id)) {
        auto frame = _frames.at(file_id);
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
#if _DISABLE_CONTOUR_COMPRESSION_
            const int compression_level = 0;
#else
            const int compression_level = std::max(0, std::min(20, settings.compression_level));
#endif
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
        }
        SendLogEvent("Error processing contours", {"contours"}, CARTA::ErrorSeverity::WARNING);
    }
    return false;
}

void Session::UpdateImageData(int file_id, bool send_image_histogram, bool channel_changed, bool stokes_changed) {
    // Send updated data for image regions with requirements when channel or stokes changes.
    // Do not send image histogram if already sent with raster data.
    if (_frames.count(file_id)) {
        if (stokes_changed) {
            SendRegionHistogramData(file_id, CUBE_REGION_ID);
            SendSpectralProfileData(file_id, CURSOR_REGION_ID, stokes_changed);
        }

        if (channel_changed || stokes_changed) {
            if (send_image_histogram) {
                SendRegionHistogramData(file_id, IMAGE_REGION_ID);
            }
            SendRegionStatsData(file_id, IMAGE_REGION_ID);
            SendSpatialProfileData(file_id, CURSOR_REGION_ID);
        }
    }
}

void Session::UpdateRegionData(int file_id, int region_id, bool channel_changed, bool stokes_changed) {
    // Send updated data for user-set regions with requirements when channel, stokes, or region changes.
    if (stokes_changed) {
        SendSpectralProfileData(file_id, region_id, stokes_changed);
    }

    if (channel_changed || stokes_changed) {
        SendRegionStatsData(file_id, region_id);
        SendRegionHistogramData(file_id, region_id);
    }

    if (!channel_changed && !stokes_changed) { // region changed, update all
        SendSpectralProfileData(file_id, region_id, stokes_changed);
        SendRegionStatsData(file_id, region_id);
        SendRegionHistogramData(file_id, region_id);
    }
}

void Session::RegionDataStreams(int file_id, int region_id) {
    bool changed(false); // channel and stokes
    if (region_id > CURSOR_REGION_ID) {
        UpdateRegionData(file_id, region_id, changed, changed);
    } else {
        // Not needed, triggered by SET_REGION which does not apply to image, cube, or cursor.
        // Added for completeness to avoid future problems.
        bool send_histogram(false);
        UpdateImageData(file_id, send_histogram, changed, changed);
    }
}

// *********************************************************************************
// SEND uWEBSOCKET MESSAGES

// Sends an event to the client with a given event name (padded/concatenated to 32 characters) and a given ProtoBuf message
void Session::SendEvent(CARTA::EventType event_type, uint32_t event_id, const google::protobuf::MessageLite& message, bool compress) {
    int message_length = message.ByteSize();
    size_t required_size = message_length + sizeof(carta::EventHeader);
    std::pair<std::vector<char>, bool> msg_vs_compress;
    std::vector<char>& msg = msg_vs_compress.first;
    msg.resize(required_size, 0);
    carta::EventHeader* head = (carta::EventHeader*)msg.data();

    head->type = event_type;
    head->icd_version = carta::ICD_VERSION;
    head->request_id = event_id;
    message.SerializeToArray(msg.data() + sizeof(carta::EventHeader), message_length);
    msg_vs_compress.second = compress;
    _out_msgs.push(msg_vs_compress);
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
    std::pair<std::vector<char>, bool> msg;
    if (_connected) {
        while (_out_msgs.try_pop(msg)) {
            _socket->send(msg.first.data(), msg.first.size(), uWS::BINARY, nullptr, nullptr, msg.second);
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

// *********************************************************************************
// ANIMATION

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
    _animation_id++;

    CARTA::StartAnimationAck ack_message;

    if (_frames.count(file_id)) {
        _frames.at(file_id)->SetAnimationViewSettings(msg.required_tiles());
        _animation_object = std::unique_ptr<AnimationObject>(new AnimationObject(
            file_id, start_frame, first_frame, last_frame, delta_frame, frame_rate, looping, reverse_at_end, always_wait));
        ack_message.set_success(true);
        ack_message.set_animation_id(_animation_id);
        ack_message.set_message("Starting animation");
        SendEvent(CARTA::EventType::START_ANIMATION_ACK, request_id, ack_message);
    } else {
        ack_message.set_success(false);
        ack_message.set_message("Incorrect file ID");
        SendEvent(CARTA::EventType::START_ANIMATION_ACK, request_id, ack_message);
    }
}

void Session::ExecuteAnimationFrameInner() {
    CARTA::AnimationFrame curr_frame;

    curr_frame = _animation_object->_next_frame;
    auto file_id(_animation_object->_file_id);
    if (_frames.count(file_id)) {
        auto frame = _frames.at(file_id);

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

            auto t_start_change_frame = std::chrono::high_resolution_clock::now();
            if (frame->SetImageChannels(channel, stokes, err_message)) {
                // Send image histogram and profiles
                bool send_histogram(true);
                UpdateImageData(file_id, send_histogram, channel_changed, stokes_changed);

                // Send contour data if required
                SendContourData(file_id);

                // Send tile data
                OnAddRequiredTiles(frame->GetAnimationViewSettings());

                // Send region histograms and profiles
                UpdateRegionData(file_id, ALL_REGIONS, channel_changed, stokes_changed);
            } else {
                if (!err_message.empty()) {
                    SendLogEvent(err_message, {"animation"}, CARTA::ErrorSeverity::ERROR);
                }
            }

            // Measure duration for frame changing as animating
            if (_verbose_logging) {
                auto t_end_change_frame = std::chrono::high_resolution_clock::now();
                auto dt_change_frame =
                    std::chrono::duration_cast<std::chrono::microseconds>(t_end_change_frame - t_start_change_frame).count();
                if (channel_changed || stokes_changed) {
                    fmt::print("Animator: Change frame in {} ms\n", dt_change_frame * 1e-3);
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
        return false;
    }

    auto wait_duration_ms = std::chrono::duration_cast<std::chrono::microseconds>(
        _animation_object->_t_last + _animation_object->_frame_interval - std::chrono::high_resolution_clock::now());

    if ((wait_duration_ms.count() < _animation_object->_wait_duration_ms) || _animation_object->_always_wait) {
        // Wait for time to execute next frame processing.
        std::this_thread::sleep_for(wait_duration_ms);

        if (_animation_object->_stop_called) {
            return false;
        }

        curr_frame = _animation_object->_next_frame;
        ExecuteAnimationFrameInner();

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

void Session::SendScriptingRequest(
    uint32_t scripting_request_id, std::string target, std::string action, std::string parameters, bool async) {
    CARTA::ScriptingRequest message;
    message.set_scripting_request_id(scripting_request_id);
    message.set_target(target);
    message.set_action(action);
    message.set_parameters(parameters);
    message.set_async(async);
    SendEvent(CARTA::EventType::SCRIPTING_REQUEST, 0, message);
}

void Session::OnScriptingResponse(const CARTA::ScriptingResponse& message, uint32_t request_id) {
    // Save response to scripting request
    int scripting_request_id(message.scripting_request_id());
    std::unique_lock<std::mutex> lock(_scripting_mutex);
    _scripting_response[scripting_request_id] = message;
}

// TODO: return pointer to original response type and copy in grpc service
bool Session::GetScriptingResponse(uint32_t scripting_request_id, CARTAVIS::ActionReply* reply) {
    std::unique_lock<std::mutex> lock(_scripting_mutex);
    auto scripting_response = _scripting_response.find(scripting_request_id);
    if (scripting_response == _scripting_response.end()) {
        return false;
    } else {
        auto msg = scripting_response->second;
        reply->set_success(msg.success());
        reply->set_message(msg.message());
        reply->set_response(msg.response());

        _scripting_response.erase(scripting_request_id);

        return true;
    }
}
