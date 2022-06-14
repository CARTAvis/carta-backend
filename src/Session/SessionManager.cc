/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018 - 2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "SessionManager.h"
#include "Logger/Logger.h"
#include "OnMessageTask.h"
#include "ThreadingManager/ThreadingManager.h"
#include "Util/Message.h"
#include "Util/Token.h"

namespace carta {

SessionManager::SessionManager(ProgramSettings& settings, std::string auth_token, std::shared_ptr<FileListHandler> file_list_handler)
    : _session_number(0), _app(uWS::App()), _settings(settings), _auth_token(auth_token), _file_list_handler(file_list_handler) {}

void SessionManager::DeleteSession(uint32_t session_id) {
    Session* session = _sessions[session_id];
    if (session) {
        spdlog::info(
            "Session {} [{}] Deleted. Remaining sessions: {}", session->GetId(), session->GetAddress(), Session::NumberOfSessions());
        session->WaitForTaskCancellation();
        session->CloseAllScriptingRequests();

        if (!session->GetRefCount()) {
            spdlog::info("Sessions in Session Map :");
            for (const std::pair<uint32_t, Session*>& ssp : _sessions) {
                Session* ss = ssp.second;
                spdlog::info("\tMap id {}, session id {}, session ptr {}", ssp.first, ss->GetId(), fmt::ptr(ss));
            }
            delete session;
            _sessions.erase(session_id);
        } else {
            spdlog::info("Session {} reference count is not 0 ({}) at this point in DeleteSession", session_id, session->GetRefCount());
        }
    } else {
        spdlog::warn("Could not delete session {}: not found!", session_id);
    }
}

void SessionManager::OnUpgrade(
    uWS::HttpResponse<false>* http_response, uWS::HttpRequest* http_request, struct us_socket_context_t* context) {
    string address;
    auto ip_header = http_request->getHeader("x-forwarded-for");
    if (!ip_header.empty()) {
        address = ip_header;
    } else {
        address = IPAsText(http_response->getRemoteAddress());
    }

    if (!ValidateAuthToken(http_request, _auth_token)) {
        spdlog::error("Incorrect or missing auth token supplied! Closing WebSocket connection");
        http_response->close();
        return;
    }

    auto now = std::chrono::system_clock::now();
    auto now_ms = std::chrono::time_point_cast<std::chrono::microseconds>(now);
    auto epoch = now_ms.time_since_epoch();
    auto value = std::chrono::duration_cast<std::chrono::microseconds>(epoch);
    _session_number = value.count();

    http_response->template upgrade<PerSocketData>({_session_number, address}, //
        http_request->getHeader("sec-websocket-key"),                          //
        http_request->getHeader("sec-websocket-protocol"),                     //
        http_request->getHeader("sec-websocket-extensions"),                   //
        context);
}

void SessionManager::OnConnect(WSType* ws) {
    auto socket_data = ws->getUserData();
    if (!socket_data) {
        spdlog::error("Error handling WebSocket connection: Socket data does not exist");
        return;
    }

    uint32_t session_id = socket_data->session_id;
    string address = socket_data->address;

    // get the uWebsockets loop
    auto* loop = uWS::Loop::get();

    // create a Session
    _sessions[session_id] = new Session(ws, loop, session_id, address, _settings.top_level_folder, _settings.starting_folder,
        _file_list_handler, _settings.read_only_mode, _settings.enable_scripting);

    _sessions[session_id]->IncreaseRefCount();

    spdlog::info("Session {} [{}] Connected. Num sessions: {}", session_id, address, Session::NumberOfSessions());
}

void SessionManager::OnDisconnect(WSType* ws, int code, std::string_view message) {
    // Skip server-forced disconnects

    spdlog::debug("WebSocket closed with code {} and message '{}'.", code, message);

    if (code == 4003) {
        return;
    }

    // Get the Session object
    uint32_t session_id = static_cast<PerSocketData*>(ws->getUserData())->session_id;

    // Delete the Session
    if (_sessions.count(session_id) > 0) {
        _sessions[session_id]->DecreaseRefCount();
        DeleteSession(session_id);
    }

    // Close the websockets
    ws->close();
}

void SessionManager::OnDrain(WSType* ws) {
    uint32_t session_id = ws->getUserData()->session_id;
    Session* session = _sessions[session_id];
    if (session) {
        spdlog::debug("Draining WebSocket backpressure: client {} [{}]. Remaining buffered amount: {} (bytes).", session->GetId(),
            session->GetAddress(), ws->getBufferedAmount());
    } else {
        spdlog::debug("Draining WebSocket backpressure: unknown client. Remaining buffered amount: {} (bytes).", ws->getBufferedAmount());
    }
}

void SessionManager::OnMessage(WSType* ws, std::string_view sv_message, uWS::OpCode op_code) {
    uint32_t session_id = static_cast<PerSocketData*>(ws->getUserData())->session_id;
    Session* session = _sessions[session_id];
    if (!session) {
        spdlog::error("Missing session!");
        return;
    }

    if (op_code == uWS::OpCode::BINARY) {
        if (sv_message.length() >= sizeof(EventHeader)) {
            session->UpdateLastMessageTimestamp();

            EventHeader head = *reinterpret_cast<const EventHeader*>(sv_message.data());
            const char* event_buf = sv_message.data() + sizeof(EventHeader);
            int event_length = sv_message.length() - sizeof(EventHeader);

            CARTA::EventType event_type = static_cast<CARTA::EventType>(head.type);
            logger::LogReceivedEventType(event_type);

            auto event_type_name = CARTA::EventType_Name(CARTA::EventType(event_type));

            bool message_parsed(false);
            OnMessageTask* tsk = nullptr;

            switch (event_type) {
                case CARTA::EventType::REGISTER_VIEWER: {
                    CARTA::RegisterViewer message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnRegisterViewer(message, head.icd_version, head.request_id);
                        message_parsed = true;
                    }
                    break;
                }
                case CARTA::EventType::RESUME_SESSION: {
                    CARTA::ResumeSession message;
                    spdlog::debug("({})({}) resuming session", fmt::ptr(session), session->GetId());
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnResumeSession(message, head.request_id);
                        message_parsed = true;
                    }
                    break;
                }
                case CARTA::EventType::SET_IMAGE_CHANNELS: {
                    CARTA::SetImageChannels message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->ImageChannelLock(message.file_id());
                        if (!session->ImageChannelTaskTestAndSet(message.file_id())) {
                            tsk = new SetImageChannelsTask(session, message.file_id());
                        }
                        // has its own queue to keep channels in order during animation
                        session->AddToSetChannelQueue(message, head.request_id);
                        session->ImageChannelUnlock(message.file_id());
                        message_parsed = true;
                    }
                    break;
                }
                case CARTA::EventType::SET_CURSOR: {
                    CARTA::SetCursor message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->AddCursorSetting(message, head.request_id);
                        tsk = new SetCursorTask(session, message.file_id());
                        message_parsed = true;
                    }
                    break;
                }
                case CARTA::EventType::SET_HISTOGRAM_REQUIREMENTS: {
                    CARTA::SetHistogramRequirements message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        if (message.histograms_size() == 0) {
                            session->CancelSetHistRequirements();
                        } else {
                            session->ResetHistContext();
                            tsk = new GeneralMessageTask<CARTA::SetHistogramRequirements>(session, message, head.request_id);
                        }
                        message_parsed = true;
                    }
                    break;
                }
                case CARTA::EventType::CLOSE_FILE: {
                    CARTA::CloseFile message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnCloseFile(message);
                        message_parsed = true;
                    }
                    break;
                }
                case CARTA::EventType::START_ANIMATION: {
                    CARTA::StartAnimation message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->CancelExistingAnimation();
                        tsk = new StartAnimationTask(session, message, head.request_id);
                        message_parsed = true;
                    }
                    break;
                }
                case CARTA::EventType::STOP_ANIMATION: {
                    CARTA::StopAnimation message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->StopAnimation(message.file_id(), message.end_frame());
                        message_parsed = true;
                    }
                    break;
                }
                case CARTA::EventType::ANIMATION_FLOW_CONTROL: {
                    CARTA::AnimationFlowControl message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->HandleAnimationFlowControlEvt(message);
                        message_parsed = true;
                    }
                    break;
                }
                case CARTA::EventType::FILE_INFO_REQUEST: {
                    CARTA::FileInfoRequest message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnFileInfoRequest(message, head.request_id);
                        message_parsed = true;
                    }
                    break;
                }
                case CARTA::EventType::OPEN_FILE: {
                    CARTA::OpenFile message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        if (!message.lel_expr()) {
                            for (auto& session_map : _sessions) {
                                session_map.second->CloseCachedImage(message.directory(), message.file());
                            }
                        }
                        session->OnOpenFile(message, head.request_id);
                        message_parsed = true;
                    }
                    break;
                }
                case CARTA::EventType::ADD_REQUIRED_TILES: {
                    CARTA::AddRequiredTiles message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        tsk = new GeneralMessageTask<CARTA::AddRequiredTiles>(session, message, head.request_id);
                        message_parsed = true;
                    }
                    break;
                }
                case CARTA::EventType::REGION_FILE_INFO_REQUEST: {
                    CARTA::RegionFileInfoRequest message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnRegionFileInfoRequest(message, head.request_id);
                        message_parsed = true;
                    }
                    break;
                }
                case CARTA::EventType::IMPORT_REGION: {
                    CARTA::ImportRegion message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnImportRegion(message, head.request_id);
                        message_parsed = true;
                    }
                    break;
                }
                case CARTA::EventType::EXPORT_REGION: {
                    CARTA::ExportRegion message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnExportRegion(message, head.request_id);
                        message_parsed = true;
                    }
                    break;
                }
                case CARTA::EventType::SET_CONTOUR_PARAMETERS: {
                    CARTA::SetContourParameters message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        tsk = new GeneralMessageTask<CARTA::SetContourParameters>(session, message, head.request_id);
                        message_parsed = true;
                    }
                    break;
                }
                case CARTA::EventType::SCRIPTING_RESPONSE: {
                    CARTA::ScriptingResponse message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnScriptingResponse(message, head.request_id);
                        message_parsed = true;
                    }
                    break;
                }
                case CARTA::EventType::SET_REGION: {
                    CARTA::SetRegion message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnSetRegion(message, head.request_id);
                        message_parsed = true;
                    }
                    break;
                }
                case CARTA::EventType::REMOVE_REGION: {
                    CARTA::RemoveRegion message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnRemoveRegion(message);
                        message_parsed = true;
                    }
                    break;
                }
                case CARTA::EventType::SET_SPECTRAL_REQUIREMENTS: {
                    CARTA::SetSpectralRequirements message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnSetSpectralRequirements(message);
                        message_parsed = true;
                    }
                    break;
                }
                case CARTA::EventType::CATALOG_FILE_INFO_REQUEST: {
                    CARTA::CatalogFileInfoRequest message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnCatalogFileInfo(message, head.request_id);
                        message_parsed = true;
                    }
                    break;
                }
                case CARTA::EventType::OPEN_CATALOG_FILE: {
                    CARTA::OpenCatalogFile message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnOpenCatalogFile(message, head.request_id);
                        message_parsed = true;
                    }
                    break;
                }
                case CARTA::EventType::CLOSE_CATALOG_FILE: {
                    CARTA::CloseCatalogFile message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnCloseCatalogFile(message);
                        message_parsed = true;
                    }
                    break;
                }
                case CARTA::EventType::CATALOG_FILTER_REQUEST: {
                    CARTA::CatalogFilterRequest message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnCatalogFilter(message, head.request_id);
                        message_parsed = true;
                    }
                    break;
                }
                case CARTA::EventType::STOP_MOMENT_CALC: {
                    CARTA::StopMomentCalc message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnStopMomentCalc(message);
                        message_parsed = true;
                    }
                    break;
                }
                case CARTA::EventType::SAVE_FILE: {
                    CARTA::SaveFile message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnSaveFile(message, head.request_id);
                        message_parsed = true;
                    }
                    break;
                }
                case CARTA::EventType::CONCAT_STOKES_FILES: {
                    CARTA::ConcatStokesFiles message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnConcatStokesFiles(message, head.request_id);
                        message_parsed = true;
                    }
                    break;
                }
                case CARTA::EventType::STOP_FILE_LIST: {
                    CARTA::StopFileList message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        if (message.file_list_type() == CARTA::Image) {
                            session->StopImageFileList();
                        } else {
                            session->StopCatalogFileList();
                        }
                        message_parsed = true;
                    }
                    break;
                }
                case CARTA::EventType::SET_SPATIAL_REQUIREMENTS: {
                    CARTA::SetSpatialRequirements message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        tsk = new GeneralMessageTask<CARTA::SetSpatialRequirements>(session, message, head.request_id);
                        message_parsed = true;
                    }
                    break;
                }
                case CARTA::EventType::SET_STATS_REQUIREMENTS: {
                    CARTA::SetStatsRequirements message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        tsk = new GeneralMessageTask<CARTA::SetStatsRequirements>(session, message, head.request_id);
                        message_parsed = true;
                    }
                    break;
                }
                case CARTA::EventType::MOMENT_REQUEST: {
                    CARTA::MomentRequest message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        tsk = new GeneralMessageTask<CARTA::MomentRequest>(session, message, head.request_id);
                        message_parsed = true;
                    }
                    break;
                }
                case CARTA::EventType::FILE_LIST_REQUEST: {
                    CARTA::FileListRequest message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        tsk = new GeneralMessageTask<CARTA::FileListRequest>(session, message, head.request_id);
                        message_parsed = true;
                    }
                    break;
                }
                case CARTA::EventType::REGION_LIST_REQUEST: {
                    CARTA::RegionListRequest message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        tsk = new GeneralMessageTask<CARTA::RegionListRequest>(session, message, head.request_id);
                        message_parsed = true;
                    }
                    break;
                }
                case CARTA::EventType::CATALOG_LIST_REQUEST: {
                    CARTA::CatalogListRequest message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        tsk = new GeneralMessageTask<CARTA::CatalogListRequest>(session, message, head.request_id);
                        message_parsed = true;
                    }
                    break;
                }
                case CARTA::EventType::PV_REQUEST: {
                    CARTA::PvRequest message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        tsk = new GeneralMessageTask<CARTA::PvRequest>(session, message, head.request_id);
                        message_parsed = true;
                    }
                    break;
                }
                case CARTA::EventType::STOP_PV_CALC: {
                    CARTA::StopPvCalc message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnStopPvCalc(message);
                        message_parsed = true;
                    }
                    break;
                }
                case CARTA::EventType::FITTING_REQUEST: {
                    CARTA::FittingRequest message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        tsk = new GeneralMessageTask<CARTA::FittingRequest>(session, message, head.request_id);
                        message_parsed = true;
                    }
                    break;
                }
                case CARTA::EventType::SET_VECTOR_OVERLAY_PARAMETERS: {
                    CARTA::SetVectorOverlayParameters message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        tsk = new GeneralMessageTask<CARTA::SetVectorOverlayParameters>(session, message, head.request_id);
                        message_parsed = true;
                    }
                    break;
                }
                default: {
                    spdlog::warn("Bad event type {}!", event_type);
                    break;
                }
            }

            if (!message_parsed) {
                spdlog::warn("Bad {} message!", event_type_name);
            }

            if (tsk) {
                ThreadManager::QueueTask(tsk);
            }
        }
    } else if (op_code == uWS::OpCode::TEXT) {
        if (sv_message == "PING") {
            auto t_session = session->GetLastMessageTimestamp();
            auto t_now = std::chrono::high_resolution_clock::now();
            auto dt = std::chrono::duration_cast<std::chrono::seconds>(t_now - t_session);
            if ((_settings.idle_session_wait_time > 0) && (dt.count() >= _settings.idle_session_wait_time)) {
                spdlog::warn("Client {} has been idle for {} seconds. Disconnecting..", session->GetId(), dt.count());
                ws->close();
            } else {
                ws->send("PONG", uWS::OpCode::TEXT);
            }
        }
    }
}

void SessionManager::Listen(std::string host, std::vector<int> ports, int default_port, int& port) {
    bool port_ok(false);

    if (ports.size() == 1) {
        // If the user specifies a valid port, we should not try other ports
        port = ports[0];
        _app.listen(host, port, LIBUS_LISTEN_EXCLUSIVE_PORT, [&](auto* token) {
            if (token) {
                port_ok = true;
            } else {
                spdlog::error("Could not listen on port {}!\n", port);
            }
        });
    } else {
        port = ports.size() > 0 ? ports[0] : default_port;
        const unsigned short port_start = port;
        const unsigned short port_end =
            ports.size() > 1 ? (ports[1] == -1 ? std::numeric_limits<unsigned short>::max() : ports[1]) : port + MAX_SOCKET_PORT_TRIALS;
        while (!port_ok) {
            if (port > port_end) {
                spdlog::error("Unable to listen on the port range {}-{}!", port_start, port - 1);
                break;
            }
            _app.listen(host, port, LIBUS_LISTEN_EXCLUSIVE_PORT, [&](auto* token) {
                if (token) {
                    port_ok = true;
                } else {
                    spdlog::warn("Port {} is already in use. Trying next port.", port);
                    ++port;
                }
            });
        }
    }
}

uWS::App& SessionManager::App() {
    return _app;
}

void SessionManager::RunApp() {
    _app.ws<PerSocketData>("/*", (uWS::App::WebSocketBehavior<PerSocketData>){.compression = uWS::DEDICATED_COMPRESSOR_256KB,
                                     .maxPayloadLength = 256 * 1024 * 1024,
                                     .maxBackpressure = 0,
                                     .upgrade = [=](uWS::HttpResponse<false>* res, uWS::HttpRequest* req,
                                                    struct us_socket_context_t* ctx) { OnUpgrade(res, req, ctx); },
                                     .open = [=](WSType* ws) { OnConnect(ws); },
                                     .message = [=](WSType* ws, std::string_view msg, uWS::OpCode code) { OnMessage(ws, msg, code); },
                                     .drain = [=](WSType* ws) { OnDrain(ws); },
                                     .close = [=](WSType* ws, int code, std::string_view msg) { OnDisconnect(ws, code, msg); }})
        .run();
}

bool SessionManager::SendScriptingRequest(int& session_id, uint32_t& scripting_request_id, std::string& target, std::string& action,
    std::string& parameters, bool& async, std::string& return_path, ScriptingResponseCallback callback,
    ScriptingSessionClosedCallback session_closed_callback) {
    Session* session = _sessions[session_id];
    if (!session) {
        return false;
    }

    CARTA::ScriptingRequest message;
    message.set_scripting_request_id(scripting_request_id);
    message.set_target(target);
    message.set_action(action);
    message.set_parameters(parameters);
    message.set_async(async);
    message.set_return_path(return_path);

    session->SendScriptingRequest(message, callback, session_closed_callback);
    return true;
}

void SessionManager::OnScriptingAbort(int session_id, uint32_t scripting_request_id) {
    Session* session = _sessions[session_id];
    if (!session) {
        return; // Session is gone; nothing to do
    }

    session->OnScriptingAbort(scripting_request_id);
}

std::string SessionManager::IPAsText(std::string_view binary) {
    std::string result;
    if (!binary.length()) {
        return result;
    }

    unsigned char* b = (unsigned char*)binary.data();
    if (binary.length() == 4) {
        result = fmt::format("{0:d}.{1:d}.{2:d}.{3:d}", b[0], b[1], b[2], b[3]);
    } else {
        result = fmt::format("::{0:x}{1:x}:{2:d}.{3:d}.{4:d}.{5:d}", b[10], b[11], b[12], b[13], b[14], b[15]);
    }

    return result;
}

} // namespace carta
