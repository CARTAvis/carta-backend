#include "Session.h"

#include <chrono>
#include <limits>
#include <memory>
#include <thread>

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include <casacore/casa/OS/File.h>

#include <carta-protobuf/defs.pb.h>
#include <carta-protobuf/error.pb.h>

#include "EventHeader.h"
#include "FileInfoLoader.h"
#include "InterfaceConstants.h"
#include "Util.h"

#define DEBUG(_DB_TEXT_) \
    {}

int Session::_num_sessions = 0;

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
      _new_frame(false),
      _image_channel_task_active(false),
      _file_settings(this) {
    _histogram_progress.fetch_and_store(HISTOGRAM_COMPLETE);
    _ref_count = 0;
    _animation_object = nullptr;
    _connected = true;

    ++_num_sessions;
    DEBUG(fprintf(stderr, "%p ::Session (%d)\n", this, _num_sessions));
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
    if (!_num_sessions)
        std::cout << "No remaining sessions." << std::endl;
}

void Session::DisconnectCalled() {
    _connected = false;
    for (auto& frame : _frames) {
        frame.second->DisconnectCalled(); // call to stop Frame's jobs and wait for jobs finished
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
                if (hdu.empty()) // use first when required
                    hdu = file_info->hdu_list(0);
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

void Session::OnRegisterViewer(const CARTA::RegisterViewer& message, uint32_t request_id) {
    auto session_id = message.session_id();
    bool success(false);
    std::string error;
    CARTA::SessionType type(CARTA::SessionType::NEW);
    // check session id
    if (!session_id) {
        session_id = _id;
        success = true;
    } else {
        type = CARTA::SessionType::RESUMED;
        if (session_id != _id) { // invalid session id
            error = fmt::format("Cannot resume session id {}", session_id);
        } else {
            success = true;
        }
    }
    _api_key = message.api_key();
    // response
    CARTA::RegisterViewerAck ack_message;
    ack_message.set_session_id(session_id);
    ack_message.set_success(success);
    ack_message.set_message(error);
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

void Session::OnOpenFile(const CARTA::OpenFile& message, uint32_t request_id) {
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
        if (hdu.empty()) // use first
            hdu = _selected_file_info->hdu_list(0);
        // form path with filename
        casacore::Path root_path(_root_folder);
        root_path.append(directory);
        root_path.append(filename);
        string abs_filename(root_path.resolvedName());

        // create Frame for open file
        auto frame = std::unique_ptr<Frame>(new Frame(_id, abs_filename, hdu, _selected_file_info_extended));
        if (frame->IsValid()) {
            std::unique_lock<std::mutex> lock(_frame_mutex); // open/close lock
            _frames[file_id] = move(frame);
            lock.unlock();
            _new_frame = true;
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
            err_message = "Could not load image";
        }
    }
    ack.set_success(success);
    ack.set_message(err_message);
    SendEvent(CARTA::EventType::OPEN_FILE_ACK, request_id, ack);
}

void Session::OnCloseFile(const CARTA::CloseFile& message) {
    auto file_id = message.file_id();
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

void Session::OnSetImageView(const CARTA::SetImageView& message) {
    auto file_id = message.file_id();
    if (_frames.count(file_id)) {
        try {
            if (_frames.at(file_id)->SetImageView(message.image_bounds(), message.mip(), message.compression_type(),
                    message.compression_quality(), message.num_subsets())) {
                SendRasterImageData(file_id, _new_frame); // send histogram only if new frame
                _new_frame = false;
            } else {
                SendLogEvent("Image view not processed", {"view"}, CARTA::ErrorSeverity::DEBUG);
            }
        } catch (std::out_of_range& range_error) {
            std::string error = fmt::format("File id {} closed", file_id);
            SendLogEvent(error, {"view"}, CARTA::ErrorSeverity::DEBUG);
        }
    } else {
        string error = fmt::format("File id {} not found", file_id);
        SendLogEvent(error, {"view"}, CARTA::ErrorSeverity::DEBUG);
    }
}

void Session::OnSetImageChannels(const CARTA::SetImageChannels& message) {
    auto file_id(message.file_id());
    if (_frames.count(file_id)) {
        try {
            std::string err_message;
            auto channel = message.channel();
            auto stokes = message.stokes();
            bool channel_changed(channel != _frames.at(file_id)->CurrentChannel());
            bool stokes_changed(stokes != _frames.at(file_id)->CurrentStokes());
            if (_frames.at(file_id)->SetImageChannels(channel, stokes, err_message)) {
                // RESPONSE: updated image raster/histogram
                SendRasterImageData(file_id, true); // true = send histogram
                // RESPONSE: region data (includes image, cursor, and set regions)
                UpdateRegionData(file_id, channel_changed, stokes_changed);
            } else {
                if (!err_message.empty())
                    SendLogEvent(err_message, {"channels"}, CARTA::ErrorSeverity::ERROR);
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

void Session::OnSetRegion(const CARTA::SetRegion& message, uint32_t request_id) {
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
                if (region_id == 0)
                    ++region_id; // reserved for cursor
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
        SendSpatialProfileData(file_id, region_id);
        SendSpectralProfileData(file_id, region_id);
        SendRegionHistogramData(file_id, region_id);
        SendRegionStatsData(file_id, region_id);
    }
}

void Session::OnRemoveRegion(const CARTA::RemoveRegion& message) {
    auto region_id(message.region_id());
    for (auto& frame : _frames) { // frames = map<fileId, unique_ptr<Frame>>
        if (frame.second)
            frame.second->RemoveRegion(region_id);
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
                _histogram_progress.fetch_and_store(HISTOGRAM_CANCEL);
                SendLogEvent("Histogram cancelled", {"histogram"}, CARTA::ErrorSeverity::INFO);
                return data_sent;
            } else {
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
                        if (!_frames.at(file_id)->GetRegionMinMax(IMAGE_REGION_ID, channel_num, stokes, min_val, max_val))
                            _frames.at(file_id)->CalcRegionMinMax(IMAGE_REGION_ID, channel_num, stokes, min_val, max_val);
                        _frames.at(file_id)->CalcRegionHistogram(
                            IMAGE_REGION_ID, channel_num, stokes, num_bins, min_val, max_val, *message_histogram);
                        // send completed histogram
                        SendFileEvent(file_id, CARTA::EventType::REGION_HISTOGRAM_DATA, request_id, histogram_message);
                        data_sent = true;
                    }
                } else { // calculate cube histogram
                    _histogram_progress.fetch_and_store(HISTOGRAM_START);
                    auto t_start = std::chrono::high_resolution_clock::now();
                    // determine cube min and max values
                    float cube_min(FLT_MAX), cube_max(FLT_MIN);
                    size_t num_channels(_frames.at(file_id)->NumChannels());
                    for (size_t chan = 0; chan < num_channels; ++chan) {
                        // min and max for this channel
                        float chan_min, chan_max;
                        if (!_frames.at(file_id)->GetRegionMinMax(IMAGE_REGION_ID, chan, stokes, chan_min, chan_max))
                            _frames.at(file_id)->CalcRegionMinMax(IMAGE_REGION_ID, chan, stokes, chan_min, chan_max);
                        cube_min = std::min(cube_min, chan_min);
                        cube_max = std::max(cube_max, chan_max);

                        // check for cancel
                        if (_histogram_progress == HISTOGRAM_CANCEL)
                            break;

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
                    if (_histogram_progress > HISTOGRAM_CANCEL)
                        _frames.at(file_id)->SetRegionMinMax(region_id, channel, stokes, cube_min, cube_max);

                    // check cancel and proceed
                    if (_histogram_progress > HISTOGRAM_CANCEL) {
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
                                std::transform(chan_histogram.bins().begin(), chan_histogram.bins().end(), cube_bins.begin(),
                                    cube_bins.begin(), std::plus<int>());
                            }

                            // check for cancel
                            if (_histogram_progress == HISTOGRAM_CANCEL)
                                break;

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
                        if (_histogram_progress > HISTOGRAM_CANCEL) {
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
                            _histogram_progress.fetch_and_store(HISTOGRAM_COMPLETE);
                        }
                    }
                    _histogram_progress.fetch_and_store(HISTOGRAM_COMPLETE);
                }
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
    _histogram_progress.fetch_and_store(progress);
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

bool Session::SendSpatialProfileData(int file_id, int region_id, bool check_current_stokes) {
    // return true if data sent
    bool data_sent(false);
    if (_frames.count(file_id)) {
        try {
            if (region_id == CURSOR_REGION_ID && !_frames.at(file_id)->IsCursorSet()) {
                return data_sent; // do not send profile unless frontend set cursor
            }
            CARTA::SpatialProfileData spatial_profile_data;
            if (_frames.at(file_id)->FillSpatialProfileData(region_id, spatial_profile_data, check_current_stokes)) {
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

bool Session::SendSpectralProfileData(int file_id, int region_id, bool check_current_stokes) {
    // return true if data sent
    bool data_sent(false);
    if (_frames.count(file_id)) {
        try {
            if (region_id == CURSOR_REGION_ID && !_frames.at(file_id)->IsCursorSet()) {
                return data_sent; // do not send profile unless frontend set cursor
            }
            CARTA::SpectralProfileData spectral_profile_data;
            if (_frames.at(file_id)->FillSpectralProfileData(region_id, spectral_profile_data, check_current_stokes)) {
                spectral_profile_data.set_file_id(file_id);
                spectral_profile_data.set_region_id(region_id);
                SendFileEvent(file_id, CARTA::EventType::SPECTRAL_PROFILE_DATA, 0, spectral_profile_data);
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

bool Session::SendRegionHistogramData(int file_id, int region_id, bool check_current_channel) {
    // return true if data sent
    bool data_sent(false);
    CARTA::RegionHistogramData* histogram_data = GetRegionHistogramData(file_id, region_id, check_current_channel);
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

void Session::UpdateRegionData(int file_id, bool channel_changed, bool stokes_changed) {
    // Send updated data for all regions with requirements
    if (_frames.count(file_id)) {
        std::vector<int> regions(_frames.at(file_id)->GetRegionIds());
        for (auto region_id : regions) {
            if (channel_changed) {
                SendSpatialProfileData(file_id, region_id);
                SendRegionHistogramData(file_id, region_id, channel_changed); // if using current channel
                SendRegionStatsData(file_id, region_id);
            }
            if (stokes_changed) {
                SendSpatialProfileData(file_id, region_id, stokes_changed);  // if using current stokes
                SendSpectralProfileData(file_id, region_id, stokes_changed); // if using current stokes
                SendRegionStatsData(file_id, region_id);
                SendRegionHistogramData(file_id, region_id);
            }
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
    if (_frames.count(file_id))
        SendEvent(event_type, event_id, message);
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
    if ((severity > CARTA::ErrorSeverity::DEBUG) || _verbose_logging)
        Log(_id, message);
}

void Session::BuildAnimationObject(CARTA::StartAnimation& msg, uint32_t request_id) {
    CARTA::AnimationFrame start_frame, end_frame, delta_frame;
    int file_id;
    uint32_t frame_interval;
    bool looping, reverse_at_end, always_wait;
    CARTA::CompressionType compression_type;
    float compression_quality;

    start_frame = msg.start_frame();
    end_frame = msg.end_frame();
    delta_frame = msg.delta_frame();
    file_id = msg.file_id();
    frame_interval = msg.frame_interval();
    looping = msg.looping();
    reverse_at_end = msg.reverse();
    compression_type = msg.compression_type();
    compression_quality = msg.compression_quality();
    always_wait = false;

    _animation_object = std::unique_ptr<AnimationObject>(new AnimationObject(file_id, start_frame, end_frame, delta_frame, frame_interval,
        looping, reverse_at_end, compression_type, compression_quality, always_wait));

    CARTA::StartAnimationAck ack_message;
    ack_message.set_success(true);
    ack_message.set_message("Starting animation");
    SendEvent(CARTA::EventType::START_ANIMATION_ACK, request_id, ack_message);
}

bool Session::ExecuteAnimationFrame() {
    if (!_animation_object) {
        std::fprintf(stderr, "%p ExecuteAnimationFrame called with null AnimationObject\n", this);
        exit(1);
    }
    if (_animation_object->_stop_called) {
        fprintf(stderr,"%p stopping at %d, %d,\n", this,
	      _animation_object->_stop_frame.channel(),  _animation_object->_stop_frame.stokes()  );
        CARTA::AnimationFrame curr_frame = _animation_object->_stop_frame;
        CARTA::AnimationFrame delta_frame = _animation_object->_delta_frame;

        auto file_id(_animation_object->_file_id);
        if (_frames.count(file_id)) {
            try {
                std::string err_message;
                auto channel = curr_frame.channel();
                auto stokes = curr_frame.stokes();
                bool channel_changed(channel != _frames.at(file_id)->CurrentChannel());
                bool stokes_changed(stokes != _frames.at(file_id)->CurrentStokes());
                if (_frames.at(file_id)->SetImageChannels(channel, stokes, err_message)) {
                    // RESPONSE: updated image raster/histogram
                    SendRasterImageData(file_id, true); // true = send histogram
                    // RESPONSE: region data (includes image, cursor, and set regions)
                    UpdateRegionData(file_id, channel_changed, stokes_changed);
                } else {
                    if (!err_message.empty())
                        SendLogEvent(err_message, {"animation"}, CARTA::ErrorSeverity::ERROR);
                }
            } catch (std::out_of_range& range_error) {
                string error = fmt::format("File id {} closed", file_id);
                SendLogEvent(error, {"animation"}, CARTA::ErrorSeverity::DEBUG);
            }
        } else {
            string error = fmt::format("File id {} not found", file_id);
            SendLogEvent(error, {"animation"}, CARTA::ErrorSeverity::DEBUG);
        }
        _animation_object->_stop_called = false;
        return false;
    }

    bool recycle_task = true;
    auto wait_duration_ms = std::chrono::duration_cast<std::chrono::microseconds>(
        _animation_object->_t_last + _animation_object->_frame_interval - std::chrono::high_resolution_clock::now());

    if ((wait_duration_ms.count() < _animation_object->_wait_duration_ms) || _animation_object->_always_wait) {
        // Wait for time to execute next frame processing.
        std::this_thread::sleep_for(wait_duration_ms);

        CARTA::AnimationFrame curr_frame = _animation_object->_current_frame;
        CARTA::AnimationFrame delta_frame = _animation_object->_delta_frame;

        auto file_id(_animation_object->_file_id);
        if (_frames.count(file_id)) {
            try {
                std::string err_message;
                auto channel = curr_frame.channel();
                auto stokes = curr_frame.stokes();
                bool channel_changed(channel != _frames.at(file_id)->CurrentChannel());
                bool stokes_changed(stokes != _frames.at(file_id)->CurrentStokes());
                if (_frames.at(file_id)->SetImageChannels(channel, stokes, err_message)) {
                    // RESPONSE: updated image raster/histogram
                    SendRasterImageData(file_id, true); // true = send histogram
                    // RESPONSE: region data (includes image, cursor, and set regions)
                    UpdateRegionData(file_id, channel_changed, stokes_changed);
                } else {
                    if (!err_message.empty())
                        SendLogEvent(err_message, {"animation"}, CARTA::ErrorSeverity::ERROR);
                }
            } catch (std::out_of_range& range_error) {
                string error = fmt::format("File id {} closed", file_id);
                SendLogEvent(error, {"animation"}, CARTA::ErrorSeverity::DEBUG);
            }
        } else {
            string error = fmt::format("File id {} not found", file_id);
            SendLogEvent(error, {"animation"}, CARTA::ErrorSeverity::DEBUG);
        }

        ::CARTA::AnimationFrame tmp_frame;

        if (_animation_object->_going_forward) {
            tmp_frame.set_channel(curr_frame.channel() + delta_frame.channel());
            tmp_frame.set_stokes(curr_frame.stokes() + delta_frame.stokes());

            if ((tmp_frame.channel() > _animation_object->_end_frame.channel()) ||
                (tmp_frame.stokes() > _animation_object->_end_frame.stokes())) {
                if (_animation_object->_reverse_at_end) {
                    _animation_object->_going_forward = false;
                } else if (_animation_object->_looping) {
                    tmp_frame.set_channel(_animation_object->_start_frame.channel());
                    tmp_frame.set_stokes(_animation_object->_start_frame.stokes());
                    _animation_object->_current_frame = tmp_frame;
                } else {
                    recycle_task = false;
                }
            } else {
                _animation_object->_current_frame = tmp_frame;
            }
        } else { // going backwards;
            tmp_frame.set_channel(curr_frame.channel() - _animation_object->_delta_frame.channel());
            tmp_frame.set_stokes(curr_frame.stokes() - _animation_object->_delta_frame.stokes());

            if ((tmp_frame.channel() < _animation_object->_start_frame.channel()) ||
                (tmp_frame.stokes() < _animation_object->_start_frame.stokes())) {
                if (_animation_object->_reverse_at_end) {
                    _animation_object->_going_forward = true;
                } else if (_animation_object->_looping) {
                    tmp_frame.set_channel(_animation_object->_end_frame.channel());
                    tmp_frame.set_stokes(_animation_object->_end_frame.stokes());
                    _animation_object->_current_frame = tmp_frame;
                } else {
                    recycle_task = false;
                }
            } else {
                _animation_object->_current_frame = tmp_frame;
            }
        }
        _animation_object->_t_last = std::chrono::high_resolution_clock::now();
    }
    return recycle_task;
}

void Session::StopAnimation(int file_id, const CARTA::AnimationFrame& frame) {
    if (!_animation_object) {
        std::fprintf(stderr, "%p Session::StopAnimation called with null AnimationObject\n", this);
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
