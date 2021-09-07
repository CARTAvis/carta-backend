/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "SessionManager.h"

#include "../EventHeader.h"
#include "../Logger/Logger.h"
#include "../OnMessageTask.h"

namespace carta {

SessionManager::SessionManager(
    ProgramSettings& settings, std::string auth_token, FileListHandler* file_list_handler, std::shared_ptr<CartaGrpcService> grpc_service)
    : _session_number(0),
      _app(uWS::App()),
      _settings(settings),
      _auth_token(auth_token),
      _file_list_handler(file_list_handler),
      _grpc_service(grpc_service) {}

void SessionManager::DeleteSession(int session_id) {
    Session* session = _sessions[session_id];
    if (session) {
        spdlog::info(
            "Client {} [{}] Deleted. Remaining sessions: {}", session->GetId(), session->GetAddress(), Session::NumberOfSessions());
        session->WaitForTaskCancellation();
        if (_grpc_service) {
            _grpc_service->RemoveSession(session);
        }
        if (!session->DecreaseRefCount()) {
            delete session;
            _sessions.erase(session_id);
        } else {
            spdlog::warn("Session {} reference count is not 0 ({}) on deletion!", session_id, session->GetRefCount());
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

    _session_number++;
    // protect against overflow
    _session_number = max(_session_number, 1u);

    http_response->template upgrade<PerSocketData>({_session_number, address}, //
        http_request->getHeader("sec-websocket-key"),                          //
        http_request->getHeader("sec-websocket-protocol"),                     //
        http_request->getHeader("sec-websocket-extensions"),                   //
        context);
}

void SessionManager::OnConnect(uWS::WebSocket<false, true, PerSocketData>* ws) {
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
        _file_list_handler, _settings.grpc_port, _settings.read_only_mode);

    if (_grpc_service) {
        _grpc_service->AddSession(_sessions[session_id]);
    }

    _sessions[session_id]->IncreaseRefCount();

    spdlog::info("Session {} [{}] Connected. Num sessions: {}", session_id, address, Session::NumberOfSessions());
}

void SessionManager::OnDisconnect(uWS::WebSocket<false, true, PerSocketData>* ws, int code, std::string_view message) {
    // Skip server-forced disconnects

    spdlog::debug("WebSocket closed with code {} and message '{}'.", code, message);

    if (code == 4003) {
        return;
    }

    // Get the Session object
    uint32_t session_id = static_cast<PerSocketData*>(ws->getUserData())->session_id;

    // Delete the Session
    DeleteSession(session_id);

    // Close the websockets
    ws->close();
}

void SessionManager::OnDrain(uWS::WebSocket<false, true, PerSocketData>* ws) {
    uint32_t session_id = ws->getUserData()->session_id;
    Session* session = _sessions[session_id];
    if (session) {
        spdlog::debug("Draining WebSocket backpressure: client {} [{}]. Remaining buffered amount: {} (bytes).", session->GetId(),
            session->GetAddress(), ws->getBufferedAmount());
    } else {
        spdlog::debug("Draining WebSocket backpressure: unknown client. Remaining buffered amount: {} (bytes).", ws->getBufferedAmount());
    }
}

void SessionManager::OnMessage(uWS::WebSocket<false, true, PerSocketData>* ws, std::string_view sv_message, uWS::OpCode op_code) {
    uint32_t session_id = static_cast<PerSocketData*>(ws->getUserData())->session_id;
    Session* session = _sessions[session_id];
    if (!session) {
        spdlog::error("Missing session!");
        return;
    }

    if (op_code == uWS::OpCode::BINARY) {
        if (sv_message.length() >= sizeof(carta::EventHeader)) {
            session->UpdateLastMessageTimestamp();

            carta::EventHeader head = *reinterpret_cast<const carta::EventHeader*>(sv_message.data());
            const char* event_buf = sv_message.data() + sizeof(carta::EventHeader);
            int event_length = sv_message.length() - sizeof(carta::EventHeader);
            OnMessageTask* tsk = nullptr;

            CARTA::EventType event_type = static_cast<CARTA::EventType>(head.type);
            LogReceivedEventType(event_type);

            switch (head.type) {
                case CARTA::EventType::REGISTER_VIEWER: {
                    CARTA::RegisterViewer message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnRegisterViewer(message, head.icd_version, head.request_id);
                    } else {
                        spdlog::warn("Bad REGISTER_VIEWER message!");
                    }
                    break;
                }
                case CARTA::EventType::RESUME_SESSION: {
                    CARTA::ResumeSession message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnResumeSession(message, head.request_id);
                    } else {
                        spdlog::warn("Bad RESUME_SESSION message!");
                    }
                    break;
                }
                case CARTA::EventType::SET_IMAGE_CHANNELS: {
                    CARTA::SetImageChannels message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->ImageChannelLock(message.file_id());
                        if (!session->ImageChannelTaskTestAndSet(message.file_id())) {
                            tsk = new (tbb::task::allocate_root(session->Context())) SetImageChannelsTask(session, message.file_id());
                        }
                        // has its own queue to keep channels in order during animation
                        session->AddToSetChannelQueue(message, head.request_id);
                        session->ImageChannelUnlock(message.file_id());
                    } else {
                        spdlog::warn("Bad SET_IMAGE_CHANNELS message!");
                    }
                    break;
                }
                case CARTA::EventType::SET_CURSOR: {
                    CARTA::SetCursor message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->AddCursorSetting(message, head.request_id);
                        tsk = new (tbb::task::allocate_root(session->Context())) SetCursorTask(session, message.file_id());
                    } else {
                        spdlog::warn("Bad SET_CURSOR message!");
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
                            tsk = new (tbb::task::allocate_root(session->HistContext()))
                                SetHistogramRequirementsTask(session, head, event_length, event_buf);
                        }
                    } else {
                        spdlog::warn("Bad SET_HISTOGRAM_REQUIREMENTS message!");
                    }
                    break;
                }
                case CARTA::EventType::CLOSE_FILE: {
                    CARTA::CloseFile message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnCloseFile(message);
                    } else {
                        spdlog::warn("Bad CLOSE_FILE message!");
                    }
                    break;
                }
                case CARTA::EventType::START_ANIMATION: {
                    CARTA::StartAnimation message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->CancelExistingAnimation();
                        session->BuildAnimationObject(message, head.request_id);
                        tsk = new (tbb::task::allocate_root(session->AnimationContext())) AnimationTask(session);
                    } else {
                        spdlog::warn("Bad START_ANIMATION message!");
                    }
                    break;
                }
                case CARTA::EventType::STOP_ANIMATION: {
                    CARTA::StopAnimation message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->StopAnimation(message.file_id(), message.end_frame());
                    } else {
                        spdlog::warn("Bad STOP_ANIMATION message!");
                    }
                    break;
                }
                case CARTA::EventType::ANIMATION_FLOW_CONTROL: {
                    CARTA::AnimationFlowControl message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->HandleAnimationFlowControlEvt(message);
                    } else {
                        spdlog::warn("Bad ANIMATION_FLOW_CONTROL message!");
                    }
                    break;
                }
                case CARTA::EventType::FILE_INFO_REQUEST: {
                    CARTA::FileInfoRequest message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnFileInfoRequest(message, head.request_id);
                    } else {
                        spdlog::warn("Bad FILE_INFO_REQUEST message!");
                    }
                    break;
                }
                case CARTA::EventType::OPEN_FILE: {
                    CARTA::OpenFile message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        for (auto& session : _sessions) {
                            session.second->CloseCachedImage(message.directory(), message.file());
                        }

                        session->OnOpenFile(message, head.request_id);
                    } else {
                        spdlog::warn("Bad OPEN_FILE message!");
                    }
                    break;
                }
                case CARTA::EventType::ADD_REQUIRED_TILES: {
                    CARTA::AddRequiredTiles message;
                    message.ParseFromArray(event_buf, event_length);
                    tsk = new (tbb::task::allocate_root(session->Context())) OnAddRequiredTilesTask(session, message);
                    break;
                }
                case CARTA::EventType::REGION_FILE_INFO_REQUEST: {
                    CARTA::RegionFileInfoRequest message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnRegionFileInfoRequest(message, head.request_id);
                    } else {
                        spdlog::warn("Bad REGION_FILE_INFO_REQUEST message!");
                    }
                    break;
                }
                case CARTA::EventType::IMPORT_REGION: {
                    CARTA::ImportRegion message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnImportRegion(message, head.request_id);
                    } else {
                        spdlog::warn("Bad IMPORT_REGION message!");
                    }
                    break;
                }
                case CARTA::EventType::EXPORT_REGION: {
                    CARTA::ExportRegion message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnExportRegion(message, head.request_id);
                    } else {
                        spdlog::warn("Bad EXPORT_REGION message!");
                    }
                    break;
                }
                case CARTA::EventType::SET_CONTOUR_PARAMETERS: {
                    CARTA::SetContourParameters message;
                    message.ParseFromArray(event_buf, event_length);
                    tsk = new (tbb::task::allocate_root(session->Context())) OnSetContourParametersTask(session, message);
                    break;
                }
                case CARTA::EventType::SCRIPTING_RESPONSE: {
                    CARTA::ScriptingResponse message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnScriptingResponse(message, head.request_id);
                    } else {
                        spdlog::warn("Bad SCRIPTING_RESPONSE message!");
                    }
                    break;
                }
                case CARTA::EventType::SET_REGION: {
                    CARTA::SetRegion message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnSetRegion(message, head.request_id);
                    } else {
                        spdlog::warn("Bad SET_REGION message!");
                    }
                    break;
                }
                case CARTA::EventType::REMOVE_REGION: {
                    CARTA::RemoveRegion message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnRemoveRegion(message);
                    } else {
                        spdlog::warn("Bad REMOVE_REGION message!");
                    }
                    break;
                }
                case CARTA::EventType::SET_SPECTRAL_REQUIREMENTS: {
                    CARTA::SetSpectralRequirements message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnSetSpectralRequirements(message);
                    } else {
                        spdlog::warn("Bad SET_SPECTRAL_REQUIREMENTS message!");
                    }
                    break;
                }
                case CARTA::EventType::CATALOG_FILE_INFO_REQUEST: {
                    CARTA::CatalogFileInfoRequest message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnCatalogFileInfo(message, head.request_id);
                    } else {
                        spdlog::warn("Bad CATALOG_FILE_INFO_REQUEST message!");
                    }
                    break;
                }
                case CARTA::EventType::OPEN_CATALOG_FILE: {
                    CARTA::OpenCatalogFile message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnOpenCatalogFile(message, head.request_id);
                    } else {
                        spdlog::warn("Bad OPEN_CATALOG_FILE message!");
                    }
                    break;
                }
                case CARTA::EventType::CLOSE_CATALOG_FILE: {
                    CARTA::CloseCatalogFile message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnCloseCatalogFile(message);
                    } else {
                        spdlog::warn("Bad CLOSE_CATALOG_FILE message!");
                    }
                    break;
                }
                case CARTA::EventType::CATALOG_FILTER_REQUEST: {
                    CARTA::CatalogFilterRequest message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnCatalogFilter(message, head.request_id);
                    } else {
                        spdlog::warn("Bad CLOSE_CATALOG_FILE message!");
                    }
                    break;
                }
                case CARTA::EventType::STOP_MOMENT_CALC: {
                    CARTA::StopMomentCalc message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnStopMomentCalc(message);
                    } else {
                        spdlog::warn("Bad STOP_MOMENT_CALC message!");
                    }
                    break;
                }
                case CARTA::EventType::SAVE_FILE: {
                    CARTA::SaveFile message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnSaveFile(message, head.request_id);
                    } else {
                        spdlog::warn("Bad SAVE_FILE message!");
                    }
                    break;
                }
                case CARTA::EventType::SPLATALOGUE_PING: {
                    CARTA::SplataloguePing message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        tsk = new (tbb::task::allocate_root(session->Context())) OnSplataloguePingTask(session, head.request_id);
                    } else {
                        spdlog::warn("Bad SPLATALOGUE_PING message!\n");
                    }
                    break;
                }
                case CARTA::EventType::SPECTRAL_LINE_REQUEST: {
                    CARTA::SpectralLineRequest message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        tsk =
                            new (tbb::task::allocate_root(session->Context())) OnSpectralLineRequestTask(session, message, head.request_id);
                    } else {
                        spdlog::warn("Bad SPECTRAL_LINE_REQUEST message!");
                    }
                    break;
                }
                case CARTA::EventType::CONCAT_STOKES_FILES: {
                    CARTA::ConcatStokesFiles message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnConcatStokesFiles(message, head.request_id);
                    } else {
                        spdlog::warn("Bad CONCAT_STOKES_FILES message!");
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
                    } else {
                        spdlog::warn("Bad STOP_FILE_LIST message!");
                    }
                    break;
                }
                default: {
                    // Copy memory into new buffer to be used and disposed by MultiMessageTask::execute
                    char* message_buffer = new char[event_length];
                    memcpy(message_buffer, event_buf, event_length);
                    tsk = new (tbb::task::allocate_root(session->Context())) MultiMessageTask(session, head, event_length, message_buffer);
                }
            }

            if (tsk) {
                tbb::task::enqueue(*tsk);
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
                                     .maxBackpressure = MAX_BACKPRESSURE,
                                     .upgrade = [=](uWS::HttpResponse<false>* res, uWS::HttpRequest* req,
                                                    struct us_socket_context_t* ctx) { OnUpgrade(res, req, ctx); },
                                     .open = [=](uWS::WebSocket<false, true, PerSocketData>* ws) { OnConnect(ws); },
                                     .message = [=](uWS::WebSocket<false, true, PerSocketData>* ws, std::string_view msg,
                                                    uWS::OpCode code) { OnMessage(ws, msg, code); },
                                     .drain = [=](uWS::WebSocket<false, true, PerSocketData>* ws) { OnDrain(ws); },
                                     .close = [=](uWS::WebSocket<false, true, PerSocketData>* ws, int code,
                                                  std::string_view msg) { OnDisconnect(ws, code, msg); }})
        .run();
}

} // namespace carta
