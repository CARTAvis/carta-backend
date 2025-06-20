/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
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

std::unordered_map<CARTA::EventType, SessionManager::MessageHandler> SessionManager::_message_handlers = {
    {CARTA::EventType::REGISTER_VIEWER, &SessionManager::RegisterViewerHandler},
    {CARTA::EventType::RESUME_SESSION, &SessionManager::ResumeSessionHandler},
    {CARTA::EventType::SET_IMAGE_CHANNELS, &SessionManager::SetImageChannelsHandler},
    {CARTA::EventType::SET_CURSOR, &SessionManager::SetCursorHandler},
    {CARTA::EventType::SET_HISTOGRAM_REQUIREMENTS, &SessionManager::SetHistogramRequirementsHandler},
    {CARTA::EventType::CLOSE_FILE, &SessionManager::CloseFileHandler},
    {CARTA::EventType::START_ANIMATION, &SessionManager::StartAnimationHandler},
    {CARTA::EventType::STOP_ANIMATION, &SessionManager::StopAnimationHandler},
    {CARTA::EventType::ANIMATION_FLOW_CONTROL, &SessionManager::AnimationFlowControlHandler},
    {CARTA::EventType::FILE_INFO_REQUEST, &SessionManager::FileInfoRequestHandler},
    {CARTA::EventType::OPEN_FILE, &SessionManager::OpenFileHandler},
    {CARTA::EventType::ADD_REQUIRED_TILES, &SessionManager::AddRequiredTilesHandler},
    {CARTA::EventType::REGION_FILE_INFO_REQUEST, &SessionManager::RegionFileInfoRequestHandler},
    {CARTA::EventType::IMPORT_REGION, &SessionManager::ImportRegionHandler},
    {CARTA::EventType::EXPORT_REGION, &SessionManager::ExportRegionHandler},
    {CARTA::EventType::SET_CONTOUR_PARAMETERS, &SessionManager::SetContourParametersHandler},
    {CARTA::EventType::SCRIPTING_RESPONSE, &SessionManager::ScriptingResponseHandler},
    {CARTA::EventType::SET_REGION, &SessionManager::SetRegionHandler},
    {CARTA::EventType::REMOVE_REGION, &SessionManager::RemoveRegionHandler},
    {CARTA::EventType::SET_SPECTRAL_REQUIREMENTS, &SessionManager::SetSpectralRequirementsHandler},
    {CARTA::EventType::CATALOG_FILE_INFO_REQUEST, &SessionManager::CatalogFileInfoRequestHandler},
    {CARTA::EventType::OPEN_CATALOG_FILE, &SessionManager::OpenCatalogFileHandler},
    {CARTA::EventType::CLOSE_CATALOG_FILE, &SessionManager::CloseCatalogFileHandler},
    {CARTA::EventType::CATALOG_FILTER_REQUEST, &SessionManager::CatalogFilterRequestHandler},
    {CARTA::EventType::STOP_MOMENT_CALC, &SessionManager::StopMomentCalcHandler},
    {CARTA::EventType::SAVE_FILE, &SessionManager::SaveFileHandler},
    {CARTA::EventType::CONCAT_STOKES_FILES, &SessionManager::ConcatStokesFilesHandler},
    {CARTA::EventType::STOP_FILE_LIST, &SessionManager::StopFileListHandler},
    {CARTA::EventType::SET_SPATIAL_REQUIREMENTS, &SessionManager::SetSpatialRequirementsHandler},
    {CARTA::EventType::SET_STATS_REQUIREMENTS, &SessionManager::SetStatsRequirementsHandler},
    {CARTA::EventType::MOMENT_REQUEST, &SessionManager::MomentRequestHandler},
    {CARTA::EventType::FILE_LIST_REQUEST, &SessionManager::FileListRequestHandler},
    {CARTA::EventType::REGION_LIST_REQUEST, &SessionManager::RegionListRequestHandler},
    {CARTA::EventType::CATALOG_LIST_REQUEST, &SessionManager::CatalogListRequestHandler},
    {CARTA::EventType::PV_REQUEST, &SessionManager::PvRequestHandler}, {CARTA::EventType::STOP_PV_CALC, &SessionManager::StopPvCalcHandler},
    {CARTA::EventType::FITTING_REQUEST, &SessionManager::FittingRequestHandler},
    {CARTA::EventType::SET_VECTOR_OVERLAY_PARAMETERS, &SessionManager::SetVectorOverlayParametersHandler},
    {CARTA::EventType::STOP_FITTING, &SessionManager::StopFittingHandler},
    {CARTA::EventType::STOP_PV_PREVIEW, &SessionManager::StopPvPreviewHandler},
    {CARTA::EventType::CLOSE_PV_PREVIEW, &SessionManager::ClosePvPreviewHandler},
    {CARTA::EventType::REMOTE_FILE_REQUEST, &SessionManager::RemoteFileRequestHandler},
    {CARTA::EventType::CHANNEL_MAP_FLOW_CONTROL, &SessionManager::ChannelMapFlowControlHandler}};

SessionManager::SessionManager(ProgramSettings& settings, std::string auth_token, std::shared_ptr<FileListHandler> file_list_handler)
    : _session_number(0), _app(uWS::App()), _settings(settings), _auth_token(auth_token), _file_list_handler(file_list_handler) {}

void SessionManager::DeleteSession(uint32_t session_id) {
    std::unique_lock<std::mutex> ulock(_sessions_mutex);
    Session* session;
    try {
        session = _sessions.at(session_id);
    } catch (const std::out_of_range& e) {
        spdlog::warn("Could not delete session {}: not found!", session_id);
        return;
    }

    spdlog::info("Session {} [{}] Deleted. Remaining sessions: {}", session->GetId(), session->GetAddress(), Session::NumberOfSessions());
    session->WaitForTaskCancellation();
    session->CloseAllScriptingRequests();

    if (!session->GetRefCount()) {
        spdlog::info("Sessions in Session Map :");
        for (const std::pair<uint32_t, Session*>& ssp : _sessions) {
            Session* ss = ssp.second;
            spdlog::info("\tMap id {}, session id {}, session ptr {}", ssp.first, ss->GetId(), fmt::ptr(ss));
        }
        _real_session_id.erase(session->GetId());
        delete session;
        _sessions.erase(session_id);
    } else {
        spdlog::info("Session {} reference count is not 0 ({}) at this point in DeleteSession", session_id, session->GetRefCount());
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
    std::unique_lock<std::mutex> ulock(_sessions_mutex);
    _sessions[session_id] = new Session(ws, loop, session_id, address, _file_list_handler);
    _sessions[session_id]->IncreaseRefCount();

    spdlog::info("Session {} [{}] Connected. Num sessions: {}", session_id, address, Session::NumberOfSessions());
}

void SessionManager::OnDisconnect(WSType* ws, int code, std::string_view message) {
    spdlog::debug("WebSocket closed with code {} and message '{}'.", code, message);

    // Skip server-forced disconnects
    if (code == 4003) {
        return;
    }

    // Get the Session object
    uint32_t session_id = static_cast<PerSocketData*>(ws->getUserData())->session_id;

    // Delete the Session
    try {
        auto session = _sessions.at(session_id);
        session->DecreaseRefCount();
        DeleteSession(session_id);
    } catch (const std::out_of_range& e) {
        // No session found
    }

    // Close the websockets
    ws->close();
}

void SessionManager::OnDrain(WSType* ws) {
    uint32_t session_id = ws->getUserData()->session_id;
    try {
        auto session = _sessions.at(session_id);
        spdlog::debug("Draining WebSocket backpressure: client {} [{}]. Remaining buffered amount: {} (bytes).", session->GetId(),
            session->GetAddress(), ws->getBufferedAmount());
    } catch (const std::out_of_range& e) {
        spdlog::debug("Draining WebSocket backpressure: unknown client. Remaining buffered amount: {} (bytes).", ws->getBufferedAmount());
    }
}

void SessionManager::OnMessage(WSType* ws, std::string_view sv_message, uWS::OpCode op_code) {
    uint32_t session_id = static_cast<PerSocketData*>(ws->getUserData())->session_id;
    Session* session;
    try {
        session = _sessions.at(session_id);
    } catch (const std::out_of_range& e) {
        spdlog::error("Missing session!");
        return;
    }

    if (op_code == uWS::OpCode::BINARY && sv_message.length() >= sizeof(EventHeader)) {
        session->UpdateLastMessageTimestamp();

        EventHeader head = Message::GetEventHeader(sv_message);
        CARTA::EventType event_type = head.GetType();

        if (!CARTA::EventType_IsValid(event_type)) {
            spdlog::error("Bad event type: {}", event_type);
            return;
        }

        logger::LogReceivedEventType(event_type);

        auto handler = _message_handlers.find(event_type);
        if (handler == _message_handlers.end()) {
            spdlog::error("Handler not found for event type: {}", CARTA::EventType_Name(event_type));
            return;
        }

        try {
            std::invoke(handler->second, this, session, sv_message, head);
        } catch (const message_parsing_exception& e) {
            spdlog::error("Error handling event {} in session {}: {}", event_type, session->GetId(), e.what());
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
    try {
        auto session = _sessions.at(_real_session_id.at(session_id));
        auto message = Message::ScriptingRequest(scripting_request_id, target, action, parameters, async, return_path);
        session->SendScriptingRequest(message, callback, session_closed_callback);
        return true;
    } catch (const std::out_of_range& e) {
        return false;
    }
}

void SessionManager::OnScriptingAbort(int session_id, uint32_t scripting_request_id) {
    try {
        auto session = _sessions.at(_real_session_id.at(session_id));
        session->OnScriptingAbort(scripting_request_id);
    } catch (const std::out_of_range& e) {
        // Session is gone; nothing to do
    }
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

void SessionManager::RegisterViewerHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::RegisterViewer message = Message::DecodeMessage<CARTA::RegisterViewer>(sv_message);
    auto real_session_id = session->GetId();
    session->OnRegisterViewer(message, head.icd_version, head.request_id);
    _real_session_id[session->GetId()] = real_session_id;
}

void SessionManager::ResumeSessionHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::ResumeSession message = Message::DecodeMessage<CARTA::ResumeSession>(sv_message);
    session->OnResumeSession(message, head.request_id);
};

void SessionManager::SetImageChannelsHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::SetImageChannels message = Message::DecodeMessage<CARTA::SetImageChannels>(sv_message);
    session->ImageChannelLock(message.file_id());
    if (!session->ImageChannelTaskTestAndSet(message.file_id())) {
        OnMessageTask* tsk = new SetImageChannelsTask(session, message.file_id());
        ThreadManager::QueueTask(tsk);
    }
    // has its own queue to keep channels in order during animation
    session->AddToSetChannelQueue(message, head.request_id);
    session->ImageChannelUnlock(message.file_id());
};

void SessionManager::SetCursorHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::SetCursor message = Message::DecodeMessage<CARTA::SetCursor>(sv_message);
    session->AddCursorSetting(message, head.request_id);
    OnMessageTask* tsk = new SetCursorTask(session, message.file_id());
    ThreadManager::QueueTask(tsk);
};

void SessionManager::SetHistogramRequirementsHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::SetHistogramRequirements message = Message::DecodeMessage<CARTA::SetHistogramRequirements>(sv_message);
    if (message.histograms_size() == 0) {
        session->CancelSetHistRequirements();
    } else {
        session->ResetHistContext();
        OnMessageTask* tsk = new GeneralMessageTask<CARTA::SetHistogramRequirements>(session, message, head.request_id);
        ThreadManager::QueueTask(tsk);
    }
};

void SessionManager::CloseFileHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::CloseFile message = Message::DecodeMessage<CARTA::CloseFile>(sv_message);
    session->OnCloseFile(message);
};

void SessionManager::StartAnimationHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::StartAnimation message = Message::DecodeMessage<CARTA::StartAnimation>(sv_message);
    session->CancelExistingAnimation();
    OnMessageTask* tsk = new StartAnimationTask(session, message, head.request_id);
    ThreadManager::QueueTask(tsk);
};

void SessionManager::StopAnimationHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::StopAnimation message = Message::DecodeMessage<CARTA::StopAnimation>(sv_message);
    session->StopAnimation(message.file_id(), message.end_frame());
};

void SessionManager::AnimationFlowControlHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::AnimationFlowControl message = Message::DecodeMessage<CARTA::AnimationFlowControl>(sv_message);
    session->HandleAnimationFlowControlEvt(message);
};

void SessionManager::FileInfoRequestHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::FileInfoRequest message = Message::DecodeMessage<CARTA::FileInfoRequest>(sv_message);
    session->OnFileInfoRequest(message, head.request_id);
};

void SessionManager::OpenFileHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::OpenFile message = Message::DecodeMessage<CARTA::OpenFile>(sv_message);
    if (!message.lel_expr()) {
        for (auto& session_map : this->_sessions) {
            session_map.second->CloseCachedImage(message.directory(), message.file());
        }
    }
    session->OnOpenFile(message, head.request_id);
};

void SessionManager::AddRequiredTilesHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::AddRequiredTiles message = Message::DecodeMessage<CARTA::AddRequiredTiles>(sv_message);
    OnMessageTask* tsk = new GeneralMessageTask<CARTA::AddRequiredTiles>(session, message, head.request_id);
    ThreadManager::QueueTask(tsk);
};

void SessionManager::RegionFileInfoRequestHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::RegionFileInfoRequest message = Message::DecodeMessage<CARTA::RegionFileInfoRequest>(sv_message);
    session->OnRegionFileInfoRequest(message, head.request_id);
};

void SessionManager::ImportRegionHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::ImportRegion message = Message::DecodeMessage<CARTA::ImportRegion>(sv_message);
    session->OnImportRegion(message, head.request_id);
};

void SessionManager::ExportRegionHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::ExportRegion message = Message::DecodeMessage<CARTA::ExportRegion>(sv_message);
    session->OnExportRegion(message, head.request_id);
};

void SessionManager::SetContourParametersHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::SetContourParameters message = Message::DecodeMessage<CARTA::SetContourParameters>(sv_message);
    OnMessageTask* tsk = new GeneralMessageTask<CARTA::SetContourParameters>(session, message, head.request_id);
    ThreadManager::QueueTask(tsk);
};

void SessionManager::ScriptingResponseHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::ScriptingResponse message = Message::DecodeMessage<CARTA::ScriptingResponse>(sv_message);
    session->OnScriptingResponse(message, head.request_id);
};

void SessionManager::SetRegionHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::SetRegion message = Message::DecodeMessage<CARTA::SetRegion>(sv_message);
    session->OnSetRegion(message, head.request_id);
};

void SessionManager::RemoveRegionHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::RemoveRegion message = Message::DecodeMessage<CARTA::RemoveRegion>(sv_message);
    session->OnRemoveRegion(message);
};

void SessionManager::SetSpectralRequirementsHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::SetSpectralRequirements message = Message::DecodeMessage<CARTA::SetSpectralRequirements>(sv_message);
    session->OnSetSpectralRequirements(message);
};

void SessionManager::CatalogFileInfoRequestHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::CatalogFileInfoRequest message = Message::DecodeMessage<CARTA::CatalogFileInfoRequest>(sv_message);
    session->OnCatalogFileInfo(message, head.request_id);
};

void SessionManager::OpenCatalogFileHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::OpenCatalogFile message = Message::DecodeMessage<CARTA::OpenCatalogFile>(sv_message);
    session->OnOpenCatalogFile(message, head.request_id);
};

void SessionManager::CloseCatalogFileHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::CloseCatalogFile message = Message::DecodeMessage<CARTA::CloseCatalogFile>(sv_message);
    session->OnCloseCatalogFile(message);
};

void SessionManager::CatalogFilterRequestHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::CatalogFilterRequest message = Message::DecodeMessage<CARTA::CatalogFilterRequest>(sv_message);
    session->OnCatalogFilter(message, head.request_id);
};

void SessionManager::StopMomentCalcHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::StopMomentCalc message = Message::DecodeMessage<CARTA::StopMomentCalc>(sv_message);
    session->OnStopMomentCalc(message);
};

void SessionManager::SaveFileHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::SaveFile message = Message::DecodeMessage<CARTA::SaveFile>(sv_message);
    session->OnSaveFile(message, head.request_id);
};

void SessionManager::ConcatStokesFilesHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::ConcatStokesFiles message = Message::DecodeMessage<CARTA::ConcatStokesFiles>(sv_message);
    session->OnConcatStokesFiles(message, head.request_id);
};

void SessionManager::StopFileListHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::StopFileList message = Message::DecodeMessage<CARTA::StopFileList>(sv_message);
    if (message.file_list_type() == CARTA::Image) {
        session->StopImageFileList();
    } else {
        session->StopCatalogFileList();
    }
};

void SessionManager::SetSpatialRequirementsHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::SetSpatialRequirements message = Message::DecodeMessage<CARTA::SetSpatialRequirements>(sv_message);
    OnMessageTask* tsk = new GeneralMessageTask<CARTA::SetSpatialRequirements>(session, message, head.request_id);
    ThreadManager::QueueTask(tsk);
};

void SessionManager::SetStatsRequirementsHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::SetStatsRequirements message = Message::DecodeMessage<CARTA::SetStatsRequirements>(sv_message);
    OnMessageTask* tsk = new GeneralMessageTask<CARTA::SetStatsRequirements>(session, message, head.request_id);
    ThreadManager::QueueTask(tsk);
};

void SessionManager::MomentRequestHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::MomentRequest message = Message::DecodeMessage<CARTA::MomentRequest>(sv_message);
    OnMessageTask* tsk = new GeneralMessageTask<CARTA::MomentRequest>(session, message, head.request_id);
    ThreadManager::QueueTask(tsk);
};

void SessionManager::FileListRequestHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::FileListRequest message = Message::DecodeMessage<CARTA::FileListRequest>(sv_message);
    OnMessageTask* tsk = new GeneralMessageTask<CARTA::FileListRequest>(session, message, head.request_id);
    ThreadManager::QueueTask(tsk);
};

void SessionManager::RegionListRequestHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::RegionListRequest message = Message::DecodeMessage<CARTA::RegionListRequest>(sv_message);
    OnMessageTask* tsk = new GeneralMessageTask<CARTA::RegionListRequest>(session, message, head.request_id);
    ThreadManager::QueueTask(tsk);
};

void SessionManager::CatalogListRequestHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::CatalogListRequest message = Message::DecodeMessage<CARTA::CatalogListRequest>(sv_message);
    OnMessageTask* tsk = new GeneralMessageTask<CARTA::CatalogListRequest>(session, message, head.request_id);
    ThreadManager::QueueTask(tsk);
};

void SessionManager::PvRequestHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::PvRequest message = Message::DecodeMessage<CARTA::PvRequest>(sv_message);
    if (message.has_preview_settings()) {
        session->StopPvPreviewUpdates(message.preview_settings().preview_id());
    }
    OnMessageTask* tsk = new GeneralMessageTask<CARTA::PvRequest>(session, message, head.request_id);
    ThreadManager::QueueTask(tsk);
};

void SessionManager::StopPvCalcHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::StopPvCalc message = Message::DecodeMessage<CARTA::StopPvCalc>(sv_message);
    session->OnStopPvCalc(message);
};

void SessionManager::FittingRequestHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::FittingRequest message = Message::DecodeMessage<CARTA::FittingRequest>(sv_message);
    OnMessageTask* tsk = new GeneralMessageTask<CARTA::FittingRequest>(session, message, head.request_id);
    ThreadManager::QueueTask(tsk);
};

void SessionManager::SetVectorOverlayParametersHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::SetVectorOverlayParameters message = Message::DecodeMessage<CARTA::SetVectorOverlayParameters>(sv_message);
    OnMessageTask* tsk = new GeneralMessageTask<CARTA::SetVectorOverlayParameters>(session, message, head.request_id);
    ThreadManager::QueueTask(tsk);
};

void SessionManager::StopFittingHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::StopFitting message = Message::DecodeMessage<CARTA::StopFitting>(sv_message);
    session->OnStopFitting(message);
};

void SessionManager::StopPvPreviewHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::StopPvPreview message = Message::DecodeMessage<CARTA::StopPvPreview>(sv_message);
    session->OnStopPvPreview(message);
};

void SessionManager::ClosePvPreviewHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::ClosePvPreview message = Message::DecodeMessage<CARTA::ClosePvPreview>(sv_message);
    session->OnClosePvPreview(message);
};

void SessionManager::RemoteFileRequestHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::RemoteFileRequest message = Message::DecodeMessage<CARTA::RemoteFileRequest>(sv_message);
    session->OnRemoteFileRequest(message, head.request_id);
};

void SessionManager::ChannelMapFlowControlHandler(Session* session, std::string_view sv_message, const EventHeader& head) {
    CARTA::ChannelMapFlowControl message = Message::DecodeMessage<CARTA::ChannelMapFlowControl>(sv_message);
    session->HandleChannelMapFlowControlEvt(message);
};

} // namespace carta
