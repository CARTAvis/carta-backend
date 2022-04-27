/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Session.h"

#include <signal.h>
#include <sys/time.h>
#include <algorithm>
#include <chrono>
#include <limits>
#include <memory>
#include <thread>
#include <tuple>
#include <vector>

#include <casacore/casa/OS/File.h>
#include <zstd.h>

#include <carta-protobuf/contour_image.pb.h>
#include <carta-protobuf/defs.pb.h>
#include <carta-protobuf/error.pb.h>
#include <carta-protobuf/raster_tile.pb.h>

#include "DataStream/Compression.h"
#include "FileList/FileExtInfoLoader.h"
#include "FileList/FileInfoLoader.h"
#include "FileList/FitsHduList.h"
#include "Frame/VectorFieldCalculator.h"
#include "ImageData/CompressedFits.h"
#include "ImageGenerators/ImageGenerator.h"
#include "Logger/Logger.h"
#include "OnMessageTask.h"
#include "SpectralLine/SpectralLineCrawler.h"
#include "ThreadingManager/ThreadingManager.h"
#include "Timer/Timer.h"
#include "Util/App.h"
#include "Util/File.h"
#include "Util/Message.h"

#ifdef _ARM_ARCH_
#include <sse2neon/sse2neon.h>
#else
#include <xmmintrin.h>
#endif

using namespace carta;

LoaderCache::LoaderCache(int capacity) : _capacity(capacity){};

std::shared_ptr<FileLoader> LoaderCache::Get(const std::string& filename, const std::string& directory) {
    std::unique_lock<std::mutex> guard(_loader_cache_mutex);

    // We have a cached loader, but the file has changed
    if (_map.find(filename) != _map.end() && _map[filename]->ImageUpdated()) {
        _map.erase(filename);
        _queue.remove(filename);
    }

    // We don't have a cached loader
    if (_map.find(filename) == _map.end()) {
        // Create the loader -- don't block while doing this
        std::shared_ptr<FileLoader> loader_ptr;
        guard.unlock();
        loader_ptr = std::shared_ptr<FileLoader>(FileLoader::GetLoader(filename, directory));
        guard.lock();

        // Check if the loader was added in the meantime
        if (_map.find(filename) == _map.end()) {
            // Evict oldest loader if necessary
            if (_map.size() == _capacity) {
                _map.erase(_queue.back());
                _queue.pop_back();
            }

            // Insert the new loader
            _map[filename] = loader_ptr;
            _queue.push_front(filename);
        }
    } else {
        // Touch the cache entry
        _queue.remove(filename);
        _queue.push_front(filename);
    }

    return _map[filename];
}

void LoaderCache::Remove(const std::string& filename) {
    std::unique_lock<std::mutex> guard(_loader_cache_mutex);
    _map.erase(filename);
    _queue.remove(filename);
}

volatile int Session::_num_sessions = 0;
int Session::_exit_after_num_seconds = 5;
bool Session::_exit_when_all_sessions_closed = false;
std::thread* Session::_animation_thread = nullptr;

Session::Session(uWS::WebSocket<false, true, PerSocketData>* ws, uWS::Loop* loop, uint32_t id, std::string address,
    std::string top_level_folder, std::string starting_folder, std::shared_ptr<FileListHandler> file_list_handler, bool read_only_mode,
    bool enable_scripting)
    : _socket(ws),
      _loop(loop),
      _id(id),
      _address(address),
      _top_level_folder(top_level_folder),
      _starting_folder(starting_folder),
      _table_controller(std::make_unique<TableController>(_top_level_folder, _starting_folder)),
      _read_only_mode(read_only_mode),
      _enable_scripting(enable_scripting),
      _region_handler(nullptr),
      _file_list_handler(file_list_handler),
      _animation_id(0),
      _animation_active(false),
      _file_settings(this),
      _loaders(LOADER_CACHE_SIZE) {
    _histogram_progress = 1.0;
    _ref_count = 0;
    _animation_object = nullptr;
    _connected = true;
    ++_num_sessions;
    UpdateLastMessageTimestamp();
    spdlog::info("{} ::Session ({}:{})", fmt::ptr(this), _id, _num_sessions);
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
            spdlog::info("No sessions timeout.");
            ThreadManager::ExitEventHandlingThreads();
            logger::FlushLogFile();
            exit(0);
        }
        alarm(1);
    }
}

Session::~Session() {
    --_num_sessions;
    spdlog::debug("{} ~Session : num sessions = {}", fmt::ptr(this), _num_sessions);
    if (!_num_sessions) {
        spdlog::info("No remaining sessions.");
        if (_exit_when_all_sessions_closed) {
            if (_exit_after_num_seconds == 0) {
                spdlog::debug("Exiting due to no sessions remaining");
                logger::FlushLogFile();
                __exit_backend_timer = 1;
            } else {
                __exit_backend_timer = _exit_after_num_seconds;
            }
            struct sigaction sig_handler;
            sig_handler.sa_handler = ExitNoSessions;
            sigemptyset(&sig_handler.sa_mask);
            sig_handler.sa_flags = 0;
            sigaction(SIGALRM, &sig_handler, nullptr);
            struct itimerval itimer;
            itimer.it_interval.tv_sec = 0;
            itimer.it_interval.tv_usec = 0;
            itimer.it_value.tv_sec = 0;
            itimer.it_value.tv_usec = 5;
            setitimer(ITIMER_REAL, &itimer, nullptr);
        }
    }
    logger::FlushLogFile();
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

void Session::WaitForTaskCancellation() {
    _connected = false;
    for (auto& frame : _frames) {
        frame.second->WaitForTaskCancellation(); // call to stop Frame's jobs and wait for jobs finished
    }
    _base_context.cancel_group_execution();
    _histogram_context.cancel_group_execution();
    if (_animation_object) {
        if (!_animation_object->_stop_called) {
            _animation_object->_stop_called = true; // stop the animation
        }
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
// File browser info

bool Session::FillExtendedFileInfo(std::map<std::string, CARTA::FileInfoExtended>& hdu_info_map, CARTA::FileInfo& file_info,
    const std::string& folder, const std::string& filename, const std::string& hdu, std::string& message) {
    // Fill CARTA::FileInfo and CARTA::FileInfoExtended
    // Map all hdus if no hdu_name supplied and FITS image
    bool file_info_ok(false);

    try {
        // FileInfo
        std::string fullname;
        if (!FillFileInfo(file_info, folder, filename, fullname, message)) {
            return file_info_ok;
        }

        // FileInfoExtended
        auto loader = _loaders.Get(fullname);
        if (!loader) {
            message = "Unsupported format.";
            return file_info_ok;
        }
        FileExtInfoLoader ext_info_loader(loader);

        std::string requested_hdu(hdu);
        if (requested_hdu.empty() && (file_info.hdu_list_size() > 0)) {
            // Use first hdu
            requested_hdu = file_info.hdu_list(0);
        }

        if (!requested_hdu.empty() || (file_info.type() != CARTA::FileType::FITS)) {
            // Get extended file info for requested hdu or images without hdus
            CARTA::FileInfoExtended file_info_ext;
            file_info_ok = ext_info_loader.FillFileExtInfo(file_info_ext, fullname, requested_hdu, message);
            if (file_info_ok) {
                hdu_info_map[requested_hdu] = file_info_ext;
            }
        } else {
            // Get extended file info for all FITS hdus
            file_info_ok = ext_info_loader.FillFitsFileInfoMap(hdu_info_map, fullname, message);
        }
    } catch (casacore::AipsError& err) {
        message = err.getMesg();
    }

    return file_info_ok;
}

bool Session::FillExtendedFileInfo(CARTA::FileInfoExtended& extended_info, CARTA::FileInfo& file_info, const std::string& folder,
    const std::string& filename, std::string& hdu, std::string& message, std::string& fullname) {
    // Fill FileInfoExtended for given file and hdu_name (may include extension name)
    bool file_info_ok(false);

    try {
        // FileInfo
        if (!FillFileInfo(file_info, folder, filename, fullname, message)) {
            return file_info_ok;
        }

        // Get file extended info loader
        auto loader = _loaders.Get(fullname);
        if (!loader) {
            message = "Unsupported format.";
            return file_info_ok;
        }
        FileExtInfoLoader ext_info_loader = FileExtInfoLoader(loader);

        // Discern hdu for extended file info
        if (hdu.empty()) {
            if (file_info.hdu_list_size() > 0) {
                hdu = file_info.hdu_list(0);
            }

            if (hdu.empty() && (file_info.type() == CARTA::FileType::FITS)) {
                // File info adds empty string for FITS
                if (IsCompressedFits(fullname)) {
                    CompressedFits cfits(fullname);
                    if (!cfits.GetFirstImageHdu(hdu)) {
                        message = "No image HDU found for FITS.";
                        return file_info_ok;
                    }
                } else {
                    std::vector<std::string> hdu_list;
                    FitsHduList fits_hdu_list(fullname);
                    fits_hdu_list.GetHduList(hdu_list, message);

                    if (hdu_list.empty()) {
                        message = "No image HDU found for FITS.";
                        return file_info_ok;
                    }

                    hdu = hdu_list[0].substr(0, hdu_list[0].find(":"));
                }
            }
        }

        file_info_ok = ext_info_loader.FillFileExtInfo(extended_info, fullname, hdu, message);
    } catch (casacore::AipsError& err) {
        message = err.getMesg();
    }

    return file_info_ok;
}

bool Session::FillExtendedFileInfo(CARTA::FileInfoExtended& extended_info, std::shared_ptr<casacore::ImageInterface<float>> image,
    const std::string& filename, std::string& message, std::shared_ptr<FileLoader>& image_loader) {
    // Fill FileInfoExtended for given image; no hdu
    bool file_info_ok(false);

    try {
        image_loader = std::shared_ptr<FileLoader>(FileLoader::GetLoader(image));
        FileExtInfoLoader ext_info_loader(image_loader);
        file_info_ok = ext_info_loader.FillFileExtInfo(extended_info, filename, "", message);
    } catch (casacore::AipsError& err) {
        message = err.getMesg();
    }

    return file_info_ok;
}

bool Session::FillFileInfo(
    CARTA::FileInfo& file_info, const std::string& folder, const std::string& filename, std::string& fullname, std::string& message) {
    // Resolve filename and fill file info submessage
    bool file_info_ok(false);

    fullname = GetResolvedFilename(_top_level_folder, folder, filename);
    if (fullname.empty()) {
        message = fmt::format("File {} does not exist.", filename);
        return file_info_ok;
    }

    file_info.set_name(filename);
    FileInfoLoader info_loader = FileInfoLoader(fullname);
    file_info_ok = info_loader.FillFileInfo(file_info);

    if (!file_info_ok) {
        message = fmt::format("File info for {} failed.", filename);
    }

    return file_info_ok;
}

// *********************************************************************************
// CARTA ICD implementation

void Session::OnRegisterViewer(const CARTA::RegisterViewer& message, uint16_t icd_version, uint32_t request_id) {
    auto session_id = message.session_id();
    bool success(true);
    std::string status;
    CARTA::SessionType type(CARTA::SessionType::NEW);

    if (icd_version != ICD_VERSION) {
        status = fmt::format("Invalid ICD version number. Expected {}, got {}", ICD_VERSION, icd_version);
        success = false;
    } else if (!session_id) {
        session_id = _id;
        status = fmt::format("Start a new frontend and assign it with session id {}", session_id);
    } else {
        type = CARTA::SessionType::RESUMED;
        if (session_id != _id) {
            spdlog::info("({}) Session setting id to {} (was {}) on resume", fmt::ptr(this), session_id, _id);
            _id = session_id;
            spdlog::info("({}) Session setting id to {}", fmt::ptr(this), session_id);
            status = fmt::format("Start a new backend and assign it with session id {}", session_id);
        } else {
            status = fmt::format("Network reconnected with session id {}", session_id);
        }
    }

    // response
    CARTA::RegisterViewerAck ack_message;
    ack_message.set_session_id(session_id);
    ack_message.set_success(success);
    ack_message.set_message(status);
    ack_message.set_session_type(type);

    auto& platform_string_map = *ack_message.mutable_platform_strings();
    platform_string_map["release_info"] = GetReleaseInformation();
#if __APPLE__
    platform_string_map["platform"] = "macOS";
#else
    platform_string_map["platform"] = "Linux";
#endif

    uint32_t feature_flags;
    if (_read_only_mode) {
        feature_flags = CARTA::ServerFeatureFlags::READ_ONLY;
    } else {
        feature_flags = CARTA::ServerFeatureFlags::SERVER_FEATURE_NONE;
    }
    if (_enable_scripting) {
        feature_flags |= CARTA::ServerFeatureFlags::SCRIPTING;
    }
    ack_message.set_server_feature_flags(feature_flags);
    SendEvent(CARTA::EventType::REGISTER_VIEWER_ACK, request_id, ack_message);
}

void Session::OnFileListRequest(const CARTA::FileListRequest& request, uint32_t request_id) {
    auto progress_callback = [&](CARTA::ListProgress progress) { SendEvent(CARTA::EventType::FILE_LIST_PROGRESS, request_id, progress); };
    _file_list_handler->SetProgressCallback(progress_callback);
    CARTA::FileListResponse response;
    FileListHandler::ResultMsg result_msg;
    _file_list_handler->OnFileListRequest(request, response, result_msg);
    if (!response.cancel()) {
        SendEvent(CARTA::EventType::FILE_LIST_RESPONSE, request_id, response);
    }
    if (!result_msg.message.empty()) {
        SendLogEvent(result_msg.message, result_msg.tags, result_msg.severity);
    }
}

void Session::OnFileInfoRequest(const CARTA::FileInfoRequest& request, uint32_t request_id) {
    CARTA::FileInfoResponse response;
    auto& file_info = *response.mutable_file_info();
    std::map<std::string, CARTA::FileInfoExtended> extended_info_map;
    string message;
    bool success = FillExtendedFileInfo(extended_info_map, file_info, request.directory(), request.file(), request.hdu(), message);

    if (success) {
        // add extended info map to message
        *response.mutable_file_info_extended() = {extended_info_map.begin(), extended_info_map.end()};
    } else {
        // log error
        spdlog::error(message);
    }

    // complete response message
    response.set_success(success);
    response.set_message(message);
    SendEvent(CARTA::EventType::FILE_INFO_RESPONSE, request_id, response);
}

void Session::OnRegionListRequest(const CARTA::RegionListRequest& request, uint32_t request_id) {
    auto progress_callback = [&](CARTA::ListProgress progress) { SendEvent(CARTA::EventType::FILE_LIST_PROGRESS, request_id, progress); };
    _file_list_handler->SetProgressCallback(progress_callback);
    CARTA::RegionListResponse response;
    FileListHandler::ResultMsg result_msg;
    _file_list_handler->OnRegionListRequest(request, response, result_msg);
    if (!response.cancel()) {
        SendEvent(CARTA::EventType::REGION_LIST_RESPONSE, request_id, response);
    }
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
    std::string hdu(message.hdu());
    auto file_id(message.file_id());
    bool is_lel_expr(message.lel_expr());

    // response message:
    CARTA::OpenFileAck ack;
    bool success(false);
    string err_message;

    if (is_lel_expr) {
        // filename field is LEL expression
        auto dir_path = GetResolvedFilename(_top_level_folder, directory, "");
        auto loader = _loaders.Get(filename, dir_path);

        try {
            loader->OpenFile(hdu);

            auto image = loader->GetImage();
            success = OnOpenFile(file_id, filename, image, &ack);
        } catch (const casacore::AipsError& err) {
            success = false;
            err_message = err.getMesg();
        }
    } else {
        ack.set_file_id(file_id);
        std::string fullname;

        // Set _loader and get file info
        CARTA::FileInfo file_info;
        CARTA::FileInfoExtended file_info_extended;
        bool info_loaded = FillExtendedFileInfo(file_info_extended, file_info, directory, filename, hdu, err_message, fullname);

        if (info_loaded) {
            // Get or create loader for frame
            auto loader = _loaders.Get(fullname);

            // Open complex image with LEL amplitude instead
            if (loader->IsComplexDataType()) {
                _loaders.Remove(filename);

                std::string expression = "AMPLITUDE(" + filename + ")";
                bool is_lel_expr(true);
                auto open_file_message = Message::OpenFile(directory, expression, hdu, file_id, message.render_mode(), is_lel_expr);
                return OnOpenFile(open_file_message, request_id, silent);
            }

            // create Frame for image
            auto frame = std::shared_ptr<Frame>(new Frame(_id, loader, hdu));

            // query loader for mipmap dataset
            bool has_mipmaps(loader->HasMip(2));

            // remove loader from the cache (if we open another copy of this file, we will need a new loader object)
            _loaders.Remove(fullname);

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
                    if (has_mipmaps) {
                        feature_flags |= CARTA::FileFeatureFlags::MIP_DATASET;
                    }
                }

                ack.set_file_feature_flags(feature_flags);
                std::vector<CARTA::Beam> beams;
                if (_frames.at(file_id)->GetBeams(beams)) {
                    *ack.mutable_beam_table() = {beams.begin(), beams.end()};
                }
                success = true;
            } else {
                err_message = frame->GetErrorMessage();
            }
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
    } else if (!err_message.empty()) {
        spdlog::error(err_message);
    }
    return success;
}

bool Session::OnOpenFile(
    int file_id, const string& name, std::shared_ptr<casacore::ImageInterface<casacore::Float>> image, CARTA::OpenFileAck* open_file_ack) {
    // Response message for opening a file
    open_file_ack->set_file_id(file_id);
    string err_message;
    std::shared_ptr<FileLoader> image_loader;

    CARTA::FileInfoExtended file_info_extended;
    bool info_loaded = FillExtendedFileInfo(file_info_extended, image, name, err_message, image_loader);
    bool success(false);

    if (info_loaded) {
        // Create Frame for image
        auto frame = std::make_unique<Frame>(_id, image_loader, "");

        if (frame->IsValid()) {
            if (_frames.count(file_id) > 0) {
                DeleteFrame(file_id);
            }
            std::unique_lock<std::mutex> lock(_frame_mutex); // open/close lock
            _frames[file_id] = move(frame);
            lock.unlock();

            // Set file info, extended file info
            CARTA::FileInfo response_file_info = CARTA::FileInfo();
            response_file_info.set_name(name);
            response_file_info.set_type(CARTA::FileType::CASA);
            *open_file_ack->mutable_file_info() = response_file_info;
            *open_file_ack->mutable_file_info_extended() = file_info_extended;
            uint32_t feature_flags = CARTA::FileFeatureFlags::FILE_FEATURE_NONE;
            open_file_ack->set_file_feature_flags(feature_flags);
            std::vector<CARTA::Beam> beams;
            if (_frames.at(file_id)->GetBeams(beams)) {
                *open_file_ack->mutable_beam_table() = {beams.begin(), beams.end()};
            }
            success = true;
        } else {
            err_message = frame->GetErrorMessage();
        }
    }

    open_file_ack->set_success(success);
    open_file_ack->set_message(err_message);

    if (success) {
        UpdateRegionData(file_id, IMAGE_REGION_ID, false, false);
    } else if (!err_message.empty()) {
        spdlog::error(err_message);
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
            frame.second->WaitForTaskCancellation(); // call to stop Frame's jobs and wait for jobs finished
            frame.second.reset();                    // delete Frame
        }
        _frames.clear();
        _image_channel_mutexes.clear();
        _image_channel_task_active.clear();
    } else if (_frames.count(file_id)) {
        _frames[file_id]->WaitForTaskCancellation(); // call to stop Frame's jobs and wait for jobs finished
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

    if (!_frames.count(file_id)) {
        return;
    }

    auto z = _frames.at(file_id)->CurrentZ();
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
        start_message.set_channel(z);
        start_message.set_stokes(stokes);
        start_message.set_animation_id(animation_id);
        start_message.set_end_sync(false);
        SendFileEvent(file_id, CARTA::EventType::RASTER_TILE_SYNC, 0, start_message);

        int num_tiles = message.tiles_size();
        CARTA::CompressionType compression_type = message.compression_type();
        float compression_quality = message.compression_quality();

        auto t_start_get_tile_data = std::chrono::high_resolution_clock::now();

        ThreadManager::ApplyThreadLimit();
#pragma omp parallel
        {
            int num_threads = omp_get_num_threads();
            int stride = std::min(num_tiles, std::min(num_threads, MAX_TILING_TASKS));
#pragma omp for
            for (int j = 0; j < stride; j++) {
                for (int i = j; i < num_tiles; i += stride) {
                    const auto& encoded_coordinate = message.tiles(i);
                    CARTA::RasterTileData raster_tile_data;
                    raster_tile_data.set_file_id(file_id);
                    raster_tile_data.set_animation_id(animation_id);
                    auto tile = Tile::Decode(encoded_coordinate);
                    if (_frames.count(file_id) &&
                        _frames.at(file_id)->FillRasterTileData(raster_tile_data, tile, z, stokes, compression_type, compression_quality)) {
                        // Only use deflate on outgoing message if the raster image compression type is NONE
                        SendFileEvent(file_id, CARTA::EventType::RASTER_TILE_DATA, 0, raster_tile_data,
                            compression_type == CARTA::CompressionType::NONE);
                    } else {
                        spdlog::warn("Discarding stale tile request for channel={}, layer={}, x={}, y={}", z, tile.layer, tile.x, tile.y);
                    }
                }
            }
        }

        // Measure duration for get tile data
        auto t_end_get_tile_data = std::chrono::high_resolution_clock::now();
        auto dt_get_tile_data = std::chrono::duration_cast<std::chrono::microseconds>(t_end_get_tile_data - t_start_get_tile_data).count();
        spdlog::performance("Get tile data group in {:.3f} ms", dt_get_tile_data * 1e-3);

        // Send final message with no tiles to signify end of the tile stream, for synchronisation purposes
        CARTA::RasterTileSync final_message;
        final_message.set_file_id(file_id);
        final_message.set_channel(z);
        final_message.set_stokes(stokes);
        final_message.set_animation_id(animation_id);
        final_message.set_end_sync(true);
        SendFileEvent(file_id, CARTA::EventType::RASTER_TILE_SYNC, 0, final_message);
    }
}

void Session::OnSetImageChannels(const CARTA::SetImageChannels& message) {
    auto file_id(message.file_id());
    std::unique_lock<std::mutex> lock(_frame_mutex);
    if (_frames.count(file_id)) {
        auto frame = _frames.at(file_id);
        std::string err_message;
        auto z_target = message.channel();
        auto stokes_target = message.stokes();
        bool z_changed(z_target != frame->CurrentZ());
        bool stokes_changed(stokes_target != frame->CurrentStokes());
        if (frame->SetImageChannels(z_target, stokes_target, err_message)) {
            // Send Contour data if required
            SendContourData(file_id);
            // Send vector field data if required
            SendVectorFieldData(file_id);
            bool send_histogram(true);
            UpdateImageData(file_id, send_histogram, z_changed, stokes_changed);
            UpdateRegionData(file_id, ALL_REGIONS, z_changed, stokes_changed);
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
            std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {
                requirements.spatial_profiles().begin(), requirements.spatial_profiles().end()};
            _frames.at(file_id)->SetSpatialRequirements(profiles);
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
    auto region_info(message.region_info());
    std::string err_message;
    bool success(false);

    if (_frames.count(file_id)) { // reference Frame for Region exists
        if (!_region_handler) {
            // created on demand only
            _region_handler = std::unique_ptr<RegionHandler>(new RegionHandler());
        }

        std::vector<CARTA::Point> points = {region_info.control_points().begin(), region_info.control_points().end()};
        RegionState region_state(file_id, region_info.region_type(), points, region_info.rotation());
        auto csys = _frames.at(file_id)->CoordinateSystem();

        success = _region_handler->SetRegion(region_id, region_state, csys);

        // log error
        if (!success) {
            err_message = fmt::format("Region {} parameters for file {} failed", region_id, file_id);
            SendLogEvent(err_message, {"region"}, CARTA::ErrorSeverity::DEBUG);
        }

        // Update the spatial profile data if it is a point region
        if (_region_handler->IsPointRegion(region_id)) {
            SendSpatialProfileDataByRegionId(region_id);
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
        OnMessageTask* tsk = new RegionDataStreamsTask(this, ALL_FILES, region_id);
        ThreadManager::QueueTask(tsk);
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
            SendFileEvent(file_id, CARTA::EventType::IMPORT_REGION_ACK, request_id, import_ack);
            return;
        }

        std::string region_file; // name or contents
        if (import_file) {
            // check that file can be opened
            region_file = GetResolvedFilename(_top_level_folder, directory, filename);
            casacore::File ccfile(region_file);
            if (!ccfile.exists() || !ccfile.isReadable()) {
                import_ack.set_success(false);
                import_ack.set_message("Import region failed: cannot open file.");
                SendFileEvent(file_id, CARTA::EventType::IMPORT_REGION_ACK, request_id, import_ack);
                return;
            }
        } else {
            // combine vector into one string
            for (auto& line : contents) {
                region_file.append(line);
            }
        }

        auto t_start_import_region = std::chrono::high_resolution_clock::now();

        if (!_region_handler) { // created on demand only
            _region_handler = std::unique_ptr<RegionHandler>(new RegionHandler());
        }

        _region_handler->ImportRegion(file_id, _frames.at(file_id), file_type, region_file, import_file, import_ack);
        // Measure duration for get tile data
        auto t_end_import_region = std::chrono::high_resolution_clock::now();
        auto dt_import_region = std::chrono::duration_cast<std::chrono::microseconds>(t_end_import_region - t_start_import_region).count();
        spdlog::performance("Import region in {:.3f} ms", dt_import_region * 1e-3);

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

        CARTA::ExportRegionAck export_ack;
        if (_read_only_mode) {
            string error = "Exporting region is not allowed in read-only mode";
            spdlog::error(error);
            SendLogEvent(error, {"Export region"}, CARTA::ErrorSeverity::ERROR);
            export_ack.set_success(false);
            export_ack.set_message(error);
        } else {
            // Export filename (optional, for server-side export)
            std::string directory(message.directory()), filename(message.file());
            std::string abs_filename;
            if (!directory.empty() && !filename.empty()) {
                // export file is on server, form path with filename
                casacore::Path top_level_path(_top_level_folder);
                top_level_path.append(directory);
                top_level_path.append(filename);
                abs_filename = top_level_path.absoluteName();
            }

            std::map<int, CARTA::RegionStyle> region_styles = {message.region_styles().begin(), message.region_styles().end()};

            _region_handler->ExportRegion(
                file_id, _frames.at(file_id), message.type(), message.coord_type(), region_styles, abs_filename, export_ack);
        }
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
        std::vector<CARTA::SetSpatialRequirements_SpatialConfig> profiles = {
            message.spatial_profiles().begin(), message.spatial_profiles().end()};
        if (region_id == CURSOR_REGION_ID) {
            _frames.at(file_id)->SetSpatialRequirements(profiles);
            SendSpatialProfileData(file_id, region_id);
        } else if (_region_handler->IsPointRegion(region_id)) {
            _region_handler->SetSpatialRequirements(region_id, file_id, _frames.at(file_id), profiles);
            SendSpatialProfileData(file_id, region_id);
        } else {
            string error = fmt::format("Spatial requirements not valid for non-cursor or non-point region ", region_id);
            SendLogEvent(error, {"spatial"}, CARTA::ErrorSeverity::ERROR);
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
            OnMessageTask* tsk = new SpectralProfileTask(this, file_id, region_id);
            ThreadManager::QueueTask(tsk);
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
        std::vector<CARTA::SetStatsRequirements_StatsConfig> requirements;
        for (size_t i = 0; i < message.stats_configs_size(); ++i) {
            requirements.push_back(message.stats_configs(i));
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
            if ((message.stats_configs_size() > 0) && !SendRegionStatsData(file_id, region_id)) {
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

void Session::OnSetContourParameters(const CARTA::SetContourParameters& message, bool silent) {
    if (_frames.count(message.file_id())) {
        const int num_levels = message.levels_size();
        if (_frames.at(message.file_id())->SetContourParameters(message) && num_levels && !silent) {
            SendContourData(message.file_id());
        }
    }
}

void Session::OnResumeSession(const CARTA::ResumeSession& message, uint32_t request_id) {
    bool success(true);
    spdlog::info("Session {} [{}] Resumed.", GetId(), GetAddress());

    // Error messages
    std::string err_message;
    std::string err_file_ids = "Problem loading files: ";
    std::string err_region_ids = "Problem loading regions: ";

    // Stop the streaming spectral profile, cube histogram and animation processes
    WaitForTaskCancellation();

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
        bool file_ok(true);

        if (image.stokes_files_size() > 1) {
            CARTA::ConcatStokesFiles concat_stokes_files_msg;
            concat_stokes_files_msg.set_file_id(image.file_id());
            *concat_stokes_files_msg.mutable_stokes_files() = image.stokes_files();

            // Open a concatenated stokes file
            if (!OnConcatStokesFiles(concat_stokes_files_msg, request_id)) {
                success = false;
                file_ok = false;
                err_file_ids.append(std::to_string(image.file_id()) + " ");
            }
        } else {
            CARTA::OpenFile open_file_msg;
            open_file_msg.set_directory(image.directory());
            open_file_msg.set_file(image.file());
            open_file_msg.set_hdu(image.hdu());
            open_file_msg.set_file_id(image.file_id());

            // Open a file
            if (!OnOpenFile(open_file_msg, request_id, true)) {
                success = false;
                file_ok = false;
                err_file_ids.append(std::to_string(image.file_id()) + " ");
            }
        }

        if (file_ok) {
            // Set image channels
            CARTA::SetImageChannels set_image_channels_msg;
            set_image_channels_msg.set_file_id(image.file_id());
            set_image_channels_msg.set_channel(image.channel());
            set_image_channels_msg.set_stokes(image.stokes());
            OnSetImageChannels(set_image_channels_msg);

            // Set regions
            for (const auto& region_id_info : image.regions()) {
                // region_id_info is <region_id, CARTA::RegionInfo>
                if (region_id_info.first == 0) {
                    CARTA::Point cursor = region_id_info.second.control_points(0);
                    CARTA::SetCursor set_cursor_msg;
                    *set_cursor_msg.mutable_point() = cursor;
                    OnSetCursor(set_cursor_msg, request_id);
                } else {
                    CARTA::SetRegion set_region_msg;
                    set_region_msg.set_file_id(image.file_id());
                    set_region_msg.set_region_id(region_id_info.first);
                    CARTA::RegionInfo resume_region_info = region_id_info.second;
                    *set_region_msg.mutable_region_info() = resume_region_info;

                    if (!OnSetRegion(set_region_msg, request_id, true)) {
                        success = false;
                        err_region_ids.append(std::to_string(region_id_info.first) + " ");
                    }
                }
            }

            // Set contours
            if (image.contour_settings().levels_size()) {
                OnSetContourParameters(image.contour_settings(), true);
            }
        }
    }

    // Open Catalog files
    for (int i = 0; i < message.catalog_files_size(); ++i) {
        const CARTA::OpenCatalogFile& open_catalog_file_msg = message.catalog_files(i);
        OnOpenCatalogFile(open_catalog_file_msg, request_id, true);
    }

    // Measure duration for resume
    auto t_end_resume = std::chrono::high_resolution_clock::now();
    auto dt_resume = std::chrono::duration_cast<std::chrono::microseconds>(t_end_resume - t_start_resume).count();
    spdlog::performance("Resume in {:.3f} ms", dt_resume * 1e-3);

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
    auto progress_callback = [&](CARTA::ListProgress progress) { SendEvent(CARTA::EventType::FILE_LIST_PROGRESS, request_id, progress); };
    _table_controller->SetProgressCallBack(progress_callback);
    CARTA::CatalogListResponse file_list_response;
    _table_controller->OnFileListRequest(file_list_request, file_list_response);
    if (!file_list_response.cancel()) {
        SendEvent(CARTA::EventType::CATALOG_LIST_RESPONSE, request_id, file_list_response);
    }
}

void Session::OnCatalogFileInfo(CARTA::CatalogFileInfoRequest file_info_request, uint32_t request_id) {
    CARTA::CatalogFileInfoResponse file_info_response;
    _table_controller->OnFileInfoRequest(file_info_request, file_info_response);
    SendEvent(CARTA::EventType::CATALOG_FILE_INFO_RESPONSE, request_id, file_info_response);
}

void Session::OnOpenCatalogFile(CARTA::OpenCatalogFile open_file_request, uint32_t request_id, bool silent) {
    CARTA::OpenCatalogFileAck open_file_response;
    _table_controller->OnOpenFileRequest(open_file_request, open_file_response);
    if (!silent) {
        SendEvent(CARTA::EventType::OPEN_CATALOG_FILE_ACK, request_id, open_file_response);
    }
}

void Session::OnCloseCatalogFile(CARTA::CloseCatalogFile close_file_request) {
    _table_controller->OnCloseFileRequest(close_file_request);
}

void Session::OnCatalogFilter(CARTA::CatalogFilterRequest filter_request, uint32_t request_id) {
    _table_controller->OnFilterRequest(filter_request, [&](const CARTA::CatalogFilterResponse& filter_response) {
        // Send partial or final results
        SendEvent(CARTA::EventType::CATALOG_FILTER_RESPONSE, request_id, filter_response);
    });
}

void Session::OnMomentRequest(const CARTA::MomentRequest& moment_request, uint32_t request_id) {
    int file_id(moment_request.file_id());
    int region_id(moment_request.region_id());

    if (_frames.count(file_id)) {
        auto& frame = _frames.at(file_id);
        // Set moment progress callback function
        auto progress_callback = [&](float progress) {
            CARTA::MomentProgress moment_progress;
            moment_progress.set_file_id(file_id);
            moment_progress.set_progress(progress);
            SendEvent(CARTA::EventType::MOMENT_PROGRESS, request_id, moment_progress);
        };

        // Do calculations
        std::vector<GeneratedImage> collapse_results;
        CARTA::MomentResponse moment_response;
        if (region_id > 0) {
            _region_handler->CalculateMoments(
                file_id, region_id, frame, progress_callback, moment_request, moment_response, collapse_results);
        } else {
            casacore::ImageRegion image_region;
            int z_min(moment_request.spectral_range().min());
            int z_max(moment_request.spectral_range().max());

            if (frame->GetImageRegion(file_id, AxisRange(z_min, z_max), frame->CurrentStokes(), image_region)) {
                frame->CalculateMoments(file_id, progress_callback, image_region, moment_request, moment_response, collapse_results);
            }
        }

        // Open moments images from the cache, open files acknowledgements will be sent to the frontend
        for (int i = 0; i < collapse_results.size(); ++i) {
            auto& collapse_result = collapse_results[i];
            auto* open_file_ack = moment_response.add_open_file_acks();
            OnOpenFile(collapse_result.file_id, collapse_result.name, collapse_result.image, open_file_ack);
        }

        // Send moment response message
        SendEvent(CARTA::EventType::MOMENT_RESPONSE, request_id, moment_response);
    } else {
        string error = fmt::format("File id {} not found", file_id);
        SendLogEvent(error, {"Moments"}, CARTA::ErrorSeverity::DEBUG);
    }
}

void Session::OnStopMomentCalc(const CARTA::StopMomentCalc& stop_moment_calc) {
    int file_id(stop_moment_calc.file_id());
    if (_frames.count(file_id)) {
        _frames.at(file_id)->StopMomentCalc();
    }
}

void Session::OnSaveFile(const CARTA::SaveFile& save_file, uint32_t request_id) {
    int file_id(save_file.file_id());
    int region_id(save_file.region_id());

    if (_frames.count(file_id)) {
        CARTA::SaveFileAck save_file_ack;
        auto active_frame = _frames.at(file_id);

        if (_read_only_mode) {
            string error = "Saving files is not allowed in read-only mode";
            spdlog::error(error);
            SendLogEvent(error, {"Saving a file"}, CARTA::ErrorSeverity::ERROR);
            save_file_ack.set_success(false);
            save_file_ack.set_message(error);
        } else if (region_id) {
            std::shared_ptr<Region> _region = _region_handler->GetRegion(region_id);
            if (_region) {
                if (active_frame->GetImageRegion(file_id, _region)) {
                    active_frame->SaveFile(_top_level_folder, save_file, save_file_ack, _region);
                } else {
                    save_file_ack.set_success(false);
                    save_file_ack.set_message("The selected region is entirely outside the image.");
                }
            }
        } else {
            // Save full image
            _frames.at(file_id)->SaveFile(_top_level_folder, save_file, save_file_ack, nullptr);
        }

        // Send response message
        SendEvent(CARTA::EventType::SAVE_FILE_ACK, request_id, save_file_ack);
    } else {
        string error = fmt::format("File id {} not found", file_id);
        SendLogEvent(error, {"Saving a file"}, CARTA::ErrorSeverity::DEBUG);
    }
}

void Session::OnSplataloguePing(uint32_t request_id) {
    CARTA::SplataloguePong splatalogue_pong;
    SpectralLineCrawler::Ping(splatalogue_pong);
    SendEvent(CARTA::EventType::SPLATALOGUE_PONG, request_id, splatalogue_pong);
}

void Session::OnSpectralLineRequest(CARTA::SpectralLineRequest spectral_line_request, uint32_t request_id) {
    CARTA::SpectralLineResponse spectral_line_response;
    SpectralLineCrawler::SendRequest(
        spectral_line_request.frequency_range(), spectral_line_request.line_intensity_lower_limit(), spectral_line_response);
    SendEvent(CARTA::EventType::SPECTRAL_LINE_RESPONSE, request_id, spectral_line_response);
}

bool Session::OnConcatStokesFiles(const CARTA::ConcatStokesFiles& message, uint32_t request_id) {
    bool success(false);
    if (!_stokes_files_connector) {
        _stokes_files_connector = std::make_unique<StokesFilesConnector>(_top_level_folder);
    }

    CARTA::ConcatStokesFilesAck response;
    std::shared_ptr<casacore::ImageConcat<float>> concatenated_image;
    string concatenated_name;

    if (_stokes_files_connector->DoConcat(message, response, concatenated_image, concatenated_name)) {
        auto* open_file_ack = response.mutable_open_file_ack();
        if (OnOpenFile(message.file_id(), concatenated_name, concatenated_image, open_file_ack)) {
            success = true;
        } else {
            spdlog::error("Fail to open the concatenated stokes image!");
        }
    } else {
        spdlog::error("Fail to concatenate stokes files!");
    }

    // Clear loaders to free images
    _stokes_files_connector->ClearCache();

    SendEvent(CARTA::EventType::CONCAT_STOKES_FILES_ACK, request_id, response);
    return success;
}

void Session::OnPvRequest(const CARTA::PvRequest& pv_request, uint32_t request_id) {
    int file_id(pv_request.file_id());
    int region_id(pv_request.region_id());
    int width(pv_request.width());
    CARTA::PvResponse pv_response;

    if (_frames.count(file_id)) {
        if (!_region_handler || (region_id <= CURSOR_REGION_ID)) {
            pv_response.set_success(false);
            pv_response.set_message("Invalid region id.");
        } else {
            auto t_start_pv_image = std::chrono::high_resolution_clock::now();

            // Set pv progress callback function
            auto progress_callback = [&](float progress) {
                CARTA::PvProgress pv_progress;
                pv_progress.set_file_id(file_id);
                pv_progress.set_progress(progress);
                SendEvent(CARTA::EventType::PV_PROGRESS, request_id, pv_progress);
            };

            auto& frame = _frames.at(file_id);
            GeneratedImage pv_image;

            if (_region_handler->CalculatePvImage(file_id, region_id, width, frame, progress_callback, pv_response, pv_image)) {
                auto* open_file_ack = pv_response.mutable_open_file_ack();
                OnOpenFile(pv_image.file_id, pv_image.name, pv_image.image, open_file_ack);
            }

            auto t_end_pv_image = std::chrono::high_resolution_clock::now();
            auto dt_pv_image = std::chrono::duration_cast<std::chrono::microseconds>(t_end_pv_image - t_start_pv_image).count();
            spdlog::performance("Generate pv image in {:.3f} ms", dt_pv_image * 1e-3);
        }

        SendEvent(CARTA::EventType::PV_RESPONSE, request_id, pv_response);
    } else {
        string error = fmt::format("File id {} not found", file_id);
        SendLogEvent(error, {"PV"}, CARTA::ErrorSeverity::DEBUG);
    }
}

void Session::OnStopPvCalc(const CARTA::StopPvCalc& stop_pv_calc) {
    int file_id(stop_pv_calc.file_id());
    if (_region_handler) {
        _region_handler->StopPvCalc(file_id);
    }
}

void Session::OnSetVectorOverlayParameters(const CARTA::SetVectorOverlayParameters& message) {
    if (_frames.count(message.file_id()) && _frames.at(message.file_id())->SetVectorOverlayParameters(message)) {
        SendVectorFieldData(message.file_id());
    }
}

// ******** SEND DATA STREAMS *********

bool Session::CalculateCubeHistogram(int file_id, CARTA::RegionHistogramData& cube_histogram_message) {
    // Calculate cube histogram message fields, and set cache in Frame
    // First try Frame::FillRegionHistogramData to get cached histogram before calculating it here.
    bool calculated(false);
    if (_frames.count(file_id)) {
        try {
            HistogramConfig cube_histogram_config;
            if (!_frames.at(file_id)->GetCubeHistogramConfig(cube_histogram_config)) {
                return calculated; // no requirements
            }

            auto t_start_cube_histogram = std::chrono::high_resolution_clock::now();
            auto num_bins = cube_histogram_config.num_bins;

            // Get stokes index
            int stokes;
            if (!_frames.at(file_id)->GetStokesTypeIndex(cube_histogram_config.coordinate, stokes)) {
                return calculated;
            }

            // To send periodic updates
            _histogram_progress = 0.0;
            auto t_start = std::chrono::high_resolution_clock::now();
            int request_id(0);
            size_t depth(_frames.at(file_id)->Depth());
            size_t total_z(depth * 2); // for progress; go through z twice, for stats then histogram

            // stats for entire cube
            BasicStats<float> cube_stats;
            for (size_t z = 0; z < depth; ++z) {
                // stats for this z
                BasicStats<float> z_stats;
                if (!_frames.at(file_id)->GetBasicStats(z, stokes, z_stats)) {
                    return calculated;
                }
                cube_stats.join(z_stats);

                // check for cancel
                if (_histogram_context.is_group_execution_cancelled()) {
                    break;
                }

                // check for progress update
                auto t_end = std::chrono::high_resolution_clock::now();
                auto dt = std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_start).count();
                if ((dt / 1e6) > UPDATE_HISTOGRAM_PROGRESS_PER_SECONDS) {
                    // send progress
                    float this_z(z);
                    float progress = this_z / total_z;
                    CARTA::RegionHistogramData progress_msg;
                    CreateCubeHistogramMessage(progress_msg, file_id, ALL_Z, stokes, progress);
                    auto* message_histogram = progress_msg.mutable_histograms();
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
                CreateCubeHistogramMessage(half_progress, file_id, ALL_Z, stokes, progress);
                auto* message_histogram = half_progress.mutable_histograms();
                SendFileEvent(file_id, CARTA::EventType::REGION_HISTOGRAM_DATA, request_id, half_progress);

                // get histogram bins for each z and accumulate bin counts in cube_bins
                Histogram z_histogram; // histogram for each z using cube stats
                Histogram cube_histogram;
                for (size_t z = 0; z < depth; ++z) {
                    if (!_frames.at(file_id)->CalculateHistogram(CUBE_REGION_ID, z, stokes, num_bins, cube_stats, z_histogram)) {
                        return calculated; // z histogram failed
                    }

                    if (z == 0) {
                        cube_histogram = std::move(z_histogram);
                    } else {
                        cube_histogram.Add(z_histogram);
                    }

                    // check for cancel
                    if (_histogram_context.is_group_execution_cancelled()) {
                        break;
                    }

                    auto t_end = std::chrono::high_resolution_clock::now();
                    auto dt = std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_start).count();
                    if ((dt / 1e6) > UPDATE_HISTOGRAM_PROGRESS_PER_SECONDS) {
                        // Send progress update
                        float this_z(z);
                        progress = 0.5 + (this_z / total_z);
                        CARTA::RegionHistogramData progress_msg;
                        CreateCubeHistogramMessage(progress_msg, file_id, ALL_Z, stokes, progress);
                        auto* message_histogram = progress_msg.mutable_histograms();
                        FillHistogram(message_histogram, cube_stats, cube_histogram);
                        SendFileEvent(file_id, CARTA::EventType::REGION_HISTOGRAM_DATA, request_id, progress_msg);
                        t_start = t_end;
                    }
                }

                // set completed cube histogram
                if (!_histogram_context.is_group_execution_cancelled()) {
                    cube_histogram_message.set_file_id(file_id);
                    cube_histogram_message.set_region_id(CUBE_REGION_ID);
                    cube_histogram_message.set_channel(ALL_Z);
                    cube_histogram_message.set_stokes(stokes);
                    cube_histogram_message.set_progress(1.0);
                    // fill histogram fields from last z histogram
                    cube_histogram_message.clear_histograms();
                    auto* message_histogram = cube_histogram_message.mutable_histograms();
                    FillHistogram(message_histogram, cube_stats, cube_histogram);

                    // cache cube histogram
                    _frames.at(file_id)->CacheCubeHistogram(stokes, cube_histogram);

                    auto t_end_cube_histogram = std::chrono::high_resolution_clock::now();
                    auto dt_cube_histogram =
                        std::chrono::duration_cast<std::chrono::microseconds>(t_end_cube_histogram - t_start_cube_histogram).count();
                    spdlog::performance("Fill cube histogram in {:.3f} ms at {:.3f} MPix/s", dt_cube_histogram * 1e-3,
                        (float)cube_stats.num_pixels / dt_cube_histogram);

                    calculated = true;
                }
            }
            _histogram_progress = 1.0;
        } catch (std::out_of_range& range_error) {
            _histogram_progress = 1.0;
            string error = fmt::format("File id {} closed", file_id);
            SendLogEvent(error, {"histogram"}, CARTA::ErrorSeverity::DEBUG);
        }
    } else {
        string error = fmt::format("File id {} not found", file_id);
        SendLogEvent(error, {"histogram"}, CARTA::ErrorSeverity::DEBUG);
    }
    return calculated;
}

void Session::CreateCubeHistogramMessage(CARTA::RegionHistogramData& msg, int file_id, int channel, int stokes, float progress) {
    // make new message and update progress
    msg.set_file_id(file_id);
    msg.set_region_id(CUBE_REGION_ID);
    msg.set_channel(channel);
    msg.set_stokes(stokes);
    msg.set_progress(progress);
    _histogram_progress = progress;
}

bool Session::SendSpatialProfileData(int file_id, int region_id) {
    // return true if data sent
    bool data_sent(false);
    std::vector<CARTA::SpatialProfileData> spatial_profile_data_vec; // spatial profile with different stokes

    auto send_results = [&](int file_id, int region_id, std::vector<CARTA::SpatialProfileData> spatial_profile_data_vec) {
        for (auto& spatial_profile_data : spatial_profile_data_vec) {
            spatial_profile_data.set_file_id(file_id);
            spatial_profile_data.set_region_id(region_id);
            SendFileEvent(file_id, CARTA::EventType::SPATIAL_PROFILE_DATA, 0, spatial_profile_data);
            data_sent = true;
        }
    };

    if ((region_id == CURSOR_REGION_ID) && _frames.count(file_id)) {
        // Cursor spatial profile
        if (_frames.at(file_id)->FillSpatialProfileData(spatial_profile_data_vec)) {
            send_results(file_id, region_id, spatial_profile_data_vec);
        }
    } else if (_region_handler->IsPointRegion(region_id) && _frames.count(file_id)) {
        // Point region spatial profile
        if (_region_handler->FillSpatialProfileData(file_id, region_id, spatial_profile_data_vec)) {
            send_results(file_id, region_id, spatial_profile_data_vec);
        }
    } else {
        string error = fmt::format("Spatial profiles not valid for non-point region {}", region_id);
        SendLogEvent(error, {"spatial"}, CARTA::ErrorSeverity::DEBUG);
    }
    return data_sent;
}

void Session::SendSpatialProfileDataByFileId(int file_id) {
    // Update spatial profile data for the cursor
    SendSpatialProfileData(file_id, CURSOR_REGION_ID);

    // Update spatial profile data for point regions
    if (_region_handler) {
        // Get region ids with respect to the given file id
        auto point_region_ids = _region_handler->GetPointRegionIds(file_id);
        for (auto point_region_id : point_region_ids) {
            SendSpatialProfileData(file_id, point_region_id);
        }
    }
}

void Session::SendSpatialProfileDataByRegionId(int region_id) {
    // Update spatial profile data for point regions
    if (_region_handler) {
        // Get file ids with respect to the region id (if a region projects on multiple files)
        auto projected_file_ids = _region_handler->GetProjectedFileIds(region_id);
        for (auto projected_file_id : projected_file_ids) {
            SendSpatialProfileData(projected_file_id, region_id);
        }
    }
}

bool Session::SendSpectralProfileData(int file_id, int region_id, bool stokes_changed) {
    // return true if data sent
    bool data_sent(false);
    if (region_id == ALL_REGIONS && !_region_handler) {
        return data_sent;
    }

    if ((region_id > CURSOR_REGION_ID) || (region_id == ALL_REGIONS) || (file_id == ALL_FILES)) {
        // Region spectral profile
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

    auto region_histogram_data_callback = [&](CARTA::RegionHistogramData histogram_data) {
        if (histogram_data.has_histograms()) {
            SendFileEvent(histogram_data.file_id(), CARTA::EventType::REGION_HISTOGRAM_DATA, 0, histogram_data);
            data_sent = true;
        }
    };

    if ((region_id > CURSOR_REGION_ID) || (region_id == ALL_REGIONS) || (file_id == ALL_FILES)) {
        // Region histogram
        data_sent = _region_handler->FillRegionHistogramData(region_histogram_data_callback, region_id, file_id);
    } else if (region_id < CURSOR_REGION_ID) {
        // Image or cube histogram
        if (_frames.count(file_id)) {
            bool filled_by_frame(_frames.at(file_id)->FillRegionHistogramData(region_histogram_data_callback, region_id, file_id));

            if (!filled_by_frame && region_id == CUBE_REGION_ID) { // not in cache, calculate cube histogram
                CARTA::RegionHistogramData histogram_data;
                histogram_data.set_file_id(file_id);
                histogram_data.set_region_id(region_id);
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

    auto region_stats_data_callback = [&](CARTA::RegionStatsData region_stats_data) {
        if (region_stats_data.statistics_size() > 0) {
            SendFileEvent(region_stats_data.file_id(), CARTA::EventType::REGION_STATS_DATA, 0, region_stats_data);
        }
    };

    if ((region_id > CURSOR_REGION_ID) || (region_id == ALL_REGIONS) || (file_id == ALL_FILES)) {
        // Region stats
        data_sent = _region_handler->FillRegionStatsData(region_stats_data_callback, region_id, file_id);
    } else if (region_id == IMAGE_REGION_ID) {
        // Image stats
        if (_frames.count(file_id)) {
            data_sent = _frames.at(file_id)->FillRegionStatsData(region_stats_data_callback, region_id, file_id);
        }
    }
    return data_sent;
}

bool Session::SendContourData(int file_id, bool ignore_empty) {
    if (_frames.count(file_id)) {
        auto frame = _frames.at(file_id);
        const ContourSettings settings = frame->GetContourParameters();
        int num_levels = settings.levels.size();

        if (!num_levels) {
            if (ignore_empty) {
                return false;
            } else {
                CARTA::ContourImageData empty_response;
                empty_response.set_file_id(file_id);
                empty_response.set_reference_file_id(settings.reference_file_id);
                empty_response.set_channel(frame->CurrentZ());
                empty_response.set_stokes(frame->CurrentStokes());
                empty_response.set_progress(1.0);
                SendFileEvent(file_id, CARTA::EventType::CONTOUR_IMAGE_DATA, 0, empty_response);
                return true;
            }
        }

        int64_t total_vertices = 0;

        auto callback = [&](double level, double progress, const std::vector<float>& vertices, const std::vector<int>& indices) {
            CARTA::ContourImageData partial_response;
            partial_response.set_file_id(file_id);
            // Currently only supports identical reference file IDs
            partial_response.set_reference_file_id(settings.reference_file_id);
            partial_response.set_channel(frame->CurrentZ());
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
            // Only use deflate compression if contours don't have ZSTD compression
            SendFileEvent(partial_response.file_id(), CARTA::EventType::CONTOUR_IMAGE_DATA, 0, partial_response, compression_level < 1);
        };

        if (frame->ContourImage(callback)) {
            return true;
        }
        SendLogEvent("Error processing contours", {"contours"}, CARTA::ErrorSeverity::WARNING);
    }
    return false;
}

void Session::UpdateImageData(int file_id, bool send_image_histogram, bool z_changed, bool stokes_changed) {
    // Send updated data for image regions with requirements when z or stokes changes.
    // Do not send image histogram if already sent with raster data.
    if (_frames.count(file_id)) {
        if (stokes_changed) {
            SendRegionHistogramData(file_id, CUBE_REGION_ID);
            SendSpectralProfileData(file_id, CURSOR_REGION_ID, stokes_changed);
        }

        if (z_changed || stokes_changed) {
            if (send_image_histogram) {
                SendRegionHistogramData(file_id, IMAGE_REGION_ID);
            }
            SendRegionStatsData(file_id, IMAGE_REGION_ID);
            SendSpatialProfileDataByFileId(file_id);
        }
    }
}

void Session::UpdateRegionData(int file_id, int region_id, bool z_changed, bool stokes_changed) {
    // Send updated data for user-set regions with requirements when z, stokes, or region changes.
    if (stokes_changed) {
        SendSpectralProfileData(file_id, region_id, stokes_changed);
    }

    if (z_changed || stokes_changed) {
        SendRegionStatsData(file_id, region_id);
        SendRegionHistogramData(file_id, region_id);
    }

    if (!z_changed && !stokes_changed) { // region changed, update all
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

bool Session::SendVectorFieldData(int file_id) {
    if (_frames.count(file_id)) {
        auto frame = _frames.at(file_id);
        auto settings = frame->GetVectorFieldParameters();
        if (settings.smoothing_factor < 1) {
            return true;
        }

        if (settings.stokes_intensity < 0 && settings.stokes_angle < 0) {
            CARTA::VectorOverlayTileData empty_response;
            empty_response.set_file_id(file_id);
            empty_response.set_channel(frame->CurrentZ());
            empty_response.set_stokes_intensity(settings.stokes_intensity);
            empty_response.set_stokes_angle(settings.stokes_angle);
            empty_response.set_progress(1.0);
            SendFileEvent(file_id, CARTA::EventType::VECTOR_OVERLAY_TILE_DATA, 0, empty_response);
            return true;
        }

        // Set callback function
        auto callback = [&](CARTA::VectorOverlayTileData& partial_response) {
            partial_response.set_file_id(file_id);
            SendFileEvent(file_id, CARTA::EventType::VECTOR_OVERLAY_TILE_DATA, 0, partial_response);
        };

        // Do PI/PA calculations
        VectorFieldCalculator vector_field_calculator(frame);
        if (vector_field_calculator.DoCalculations(callback)) {
            return true;
        }
        SendLogEvent("Error processing vector field image", {"vector field"}, CARTA::ErrorSeverity::WARNING);
    }
    return false;
}

// *********************************************************************************
// SEND uWEBSOCKET MESSAGES

// Sends an event to the client with a given event name (padded/concatenated to 32 characters) and a given ProtoBuf message
void Session::SendEvent(CARTA::EventType event_type, uint32_t event_id, const google::protobuf::MessageLite& message, bool compress) {
    logger::LogSentEventType(event_type);

    size_t message_length = message.ByteSizeLong();
    size_t required_size = message_length + sizeof(EventHeader);
    std::pair<std::vector<char>, bool> msg_vs_compress;
    std::vector<char>& msg = msg_vs_compress.first;
    msg.resize(required_size, 0);
    EventHeader* head = (EventHeader*)msg.data();

    head->type = event_type;
    head->icd_version = ICD_VERSION;
    head->request_id = event_id;
    message.SerializeToArray(msg.data() + sizeof(EventHeader), message_length);
    // Skip compression on files smaller than 1 kB
    msg_vs_compress.second = compress && required_size > 1024;
    _out_msgs.push(msg_vs_compress);

    // uWS::Loop::defer(function) is the only thread-safe function, use it to defer the calling of a function to the thread that runs the
    // Loop.
    if (_loop && _socket) {
        _loop->defer([&]() {
            std::pair<std::vector<char>, bool> msg;
            if (_connected) {
                while (_out_msgs.try_pop(msg)) {
                    std::string_view sv(msg.first.data(), msg.first.size());
                    _socket->cork([&]() {
                        auto status = _socket->send(sv, uWS::OpCode::BINARY, msg.second);
                        if (status == uWS::WebSocket<false, true, PerSocketData>::DROPPED) {
                            spdlog::error("Failed to send message of size {} kB", sv.size() / 1024.0);
                        }
                    });
                }
            }
        });
    }
}

void Session::SendFileEvent(
    int32_t file_id, CARTA::EventType event_type, uint32_t event_id, google::protobuf::MessageLite& message, bool compress) {
    // do not send if file is closed
    if (_frames.count(file_id)) {
        SendEvent(event_type, event_id, message, compress);
    }
}

void Session::SendLogEvent(const std::string& message, std::vector<std::string> tags, CARTA::ErrorSeverity severity) {
    CARTA::ErrorData error_data;
    error_data.set_message(message);
    error_data.set_severity(severity);
    *error_data.mutable_tags() = {tags.begin(), tags.end()};
    SendEvent(CARTA::EventType::ERROR_DATA, 0, error_data);
    if ((severity > CARTA::ErrorSeverity::DEBUG)) {
        spdlog::debug("Session {}: {}", _id, message);
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
        _animation_object = std::unique_ptr<AnimationObject>(new AnimationObject(file_id, start_frame, first_frame, last_frame, delta_frame,
            msg.matched_frames(), frame_rate, looping, reverse_at_end, always_wait));
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
    auto active_file_id(_animation_object->_file_id);
    if (_frames.count(active_file_id)) {
        auto active_frame = _frames.at(active_file_id);

        try {
            std::string err_message;
            auto active_frame_z = curr_frame.channel();
            auto active_frame_stokes = curr_frame.stokes();

            if ((_animation_object->_context).is_group_execution_cancelled()) {
                return;
            }

            bool z_changed(active_frame_z != active_frame->CurrentZ());
            bool stokes_changed(active_frame_stokes != active_frame->CurrentStokes());

            _animation_object->_current_frame = curr_frame;
            auto offset = active_frame_z - _animation_object->_first_frame.channel();

            auto t_start_change_frame = std::chrono::high_resolution_clock::now();

            if (z_changed && offset >= 0 && !_animation_object->_matched_frames.empty()) {
                std::vector<int32_t> file_ids_to_update;
                // Update z sequentially
                for (auto& entry : _animation_object->_matched_frames) {
                    auto file_id = entry.first;
                    auto& frame_numbers = entry.second;
                    bool is_active_frame = file_id == active_file_id;
                    if (_frames.count(file_id)) {
                        auto& frame = _frames.at(file_id);
                        // Skip out of bounds frames
                        if (!is_active_frame && offset >= frame_numbers.size()) {
                            spdlog::error("Animator: Missing entries in matched frame list for file {}", file_id);
                            continue;
                        }
                        float z_val = is_active_frame ? active_frame_z : frame_numbers[offset];
                        if (std::isfinite(z_val)) {
                            int rounded_z;
                            if (is_active_frame) {
                                rounded_z = active_frame_z;
                            } else {
                                rounded_z = std::round(std::clamp(z_val, 0.0f, (float)(frame->Depth() - 1)));
                            }
                            if (rounded_z != frame->CurrentZ() && frame->SetImageChannels(rounded_z, frame->CurrentStokes(), err_message)) {
                                // Send image histogram and profiles
                                // TODO: do we need to send this?
                                UpdateImageData(file_id, true, z_changed, stokes_changed);
                                file_ids_to_update.push_back(file_id);
                            } else {
                                if (!err_message.empty()) {
                                    SendLogEvent(err_message, {"animation"}, CARTA::ErrorSeverity::ERROR);
                                }
                            }
                        }
                    } else {
                        spdlog::error("Animator: Missing matched frame list for file {}", file_id);
                    }
                }
                // Calculate and send images, contours and profiles
                auto num_files = file_ids_to_update.size();
                for (auto i = 0; i < num_files; i++) {
                    auto file_id = file_ids_to_update[i];
                    bool is_active_frame = file_id == active_file_id;
                    // Send contour data if required. Empty contour data messages are sent if there are no contour levels
                    SendContourData(file_id, is_active_frame);

                    // Send vector field data if required
                    SendVectorFieldData(file_id);

                    // Send tile data for active frame
                    if (is_active_frame) {
                        OnAddRequiredTiles(active_frame->GetAnimationViewSettings());
                    }

                    // Send region histograms and profiles
                    UpdateRegionData(file_id, ALL_REGIONS, z_changed, stokes_changed);
                }
            } else {
                if (active_frame->SetImageChannels(active_frame_z, active_frame_stokes, err_message)) {
                    // Send image histogram and profiles
                    bool send_histogram(true);
                    UpdateImageData(active_file_id, send_histogram, z_changed, stokes_changed);

                    // Send contour data if required
                    SendContourData(active_file_id);

                    // Send vector field data if required
                    SendVectorFieldData(active_file_id);

                    // Send tile data
                    OnAddRequiredTiles(active_frame->GetAnimationViewSettings());

                    // Send region histograms and profiles
                    UpdateRegionData(active_file_id, ALL_REGIONS, z_changed, stokes_changed);
                } else {
                    if (!err_message.empty()) {
                        SendLogEvent(err_message, {"animation"}, CARTA::ErrorSeverity::ERROR);
                    }
                }
            }

            // Measure duration for frame changing as animating
            auto t_end_change_frame = std::chrono::high_resolution_clock::now();
            auto dt_change_frame = std::chrono::duration_cast<std::chrono::microseconds>(t_end_change_frame - t_start_change_frame).count();
            if (z_changed || stokes_changed) {
                spdlog::performance("Animator: Change frame in {:.3f} ms", dt_change_frame * 1e-3);
            }
        } catch (std::out_of_range& range_error) {
            string error = fmt::format("File id {} closed", active_file_id);
            SendLogEvent(error, {"animation"}, CARTA::ErrorSeverity::DEBUG);
        }
    } else {
        string error = fmt::format("File id {} not found", active_file_id);
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
            tmp_frame.set_channel(curr_frame.channel() - delta_frame.channel());
            tmp_frame.set_stokes(curr_frame.stokes() - delta_frame.stokes());

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
        spdlog::error(
            "{} Session::StopAnimation called with file id {}. Expected file id {}", fmt::ptr(this), file_id, _animation_object->_file_id);
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
            gap = (_animation_object->_last_flow_frame).stokes() - _animation_object->_current_frame.stokes();
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
            OnMessageTask* tsk = new AnimationTask(this);
            ThreadManager::QueueTask(tsk);
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
    }
}

void Session::SendScriptingRequest(
    CARTA::ScriptingRequest& message, ScriptingResponseCallback callback, ScriptingSessionClosedCallback session_closed_callback) {
    int scripting_request_id(message.scripting_request_id());
    SendEvent(CARTA::EventType::SCRIPTING_REQUEST, 0, message);
    std::unique_lock<std::mutex> lock(_scripting_mutex);
    _scripting_callbacks[scripting_request_id] = std::make_tuple(callback, session_closed_callback);
}

void Session::OnScriptingResponse(const CARTA::ScriptingResponse& message, uint32_t request_id) {
    int scripting_request_id(message.scripting_request_id());

    std::unique_lock<std::mutex> lock(_scripting_mutex);
    auto callback_iter = _scripting_callbacks.find(scripting_request_id);
    if (callback_iter == _scripting_callbacks.end()) {
        spdlog::warn("Could not find callback for scripting response with request ID {}.", scripting_request_id);
    } else {
        auto [callback, session_closed_callback] = callback_iter->second;
        callback(message.success(), message.message(), message.response());
        _scripting_callbacks.erase(scripting_request_id);
    }
}

void Session::OnScriptingAbort(uint32_t scripting_request_id) {
    std::unique_lock<std::mutex> lock(_scripting_mutex);
    _scripting_callbacks.erase(scripting_request_id);
}

void Session::CloseAllScriptingRequests() {
    std::unique_lock<std::mutex> lock(_scripting_mutex);
    for (auto& [key, callbacks] : _scripting_callbacks) {
        auto [callback, session_closed_callback] = callbacks;
        session_closed_callback();
    }
    _scripting_callbacks.clear();
}

void Session::StopImageFileList() {
    if (_file_list_handler) {
        _file_list_handler->StopGettingFileList();
    }
}

void Session::StopCatalogFileList() {
    if (_table_controller) {
        _table_controller->StopGettingFileList();
    }
}

void Session::UpdateLastMessageTimestamp() {
    _last_message_timestamp = std::chrono::high_resolution_clock::now();
}

std::chrono::high_resolution_clock::time_point Session::GetLastMessageTimestamp() {
    return _last_message_timestamp;
}

void Session::CloseCachedImage(const std::string& directory, const std::string& file) {
    std::string fullname = GetResolvedFilename(_top_level_folder, directory, file);
    for (auto& frame : _frames) {
        frame.second->CloseCachedImage(fullname);
    }
}
