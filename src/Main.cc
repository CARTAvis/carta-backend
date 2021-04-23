/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <thread>
#include <tuple>
#include <vector>

#include <curl/curl.h>
#include <signal.h>
#include <tbb/task.h>
#include <tbb/task_scheduler_init.h>
#include <uWebSockets/App.h>
#include <uuid/uuid.h>

#include "EventHeader.h"
#include "FileList/FileListHandler.h"
#include "FileSettings.h"
#include "GrpcServer/CartaGrpcService.h"
#include "Logger/Logger.h"
#include "OnMessageTask.h"
#include "Session.h"
#include "SessionManager/ProgramSettings.h"
#include "SimpleFrontendServer/SimpleFrontendServer.h"
#include "Threading.h"
#include "Util.h"

using namespace std;

// file list handler for the file browser
static FileListHandler* file_list_handler;
SimpleFrontendServer* http_server;

static uint32_t session_number;

// grpc server for scripting client
static std::unique_ptr<CartaGrpcService> carta_grpc_service;
static std::unique_ptr<grpc::Server> carta_grpc_server;

static string auth_token = "";

carta::ProgramSettings settings;
// Sessions map
std::unordered_map<uint32_t, Session*> sessions;

// Apply ws->getUserData and return one of these
struct PerSocketData {
    uint32_t session_id;
    string address;
};

void DeleteSession(int session_id) {
    Session* session = sessions[session_id];
    if (session) {
        spdlog::info(
            "Client {} [{}] Deleted. Remaining sessions: {}", session->GetId(), session->GetAddress(), Session::NumberOfSessions());
        session->WaitForTaskCancellation();
        if (carta_grpc_service) {
            carta_grpc_service->RemoveSession(session);
        }
        if (!session->DecreaseRefCount()) {
            delete session;
            sessions.erase(session_id);
        } else {
            spdlog::warn("Session {} reference count is not 0 ({}) on deletion!", session_id, session->GetRefCount());
        }
    } else {
        spdlog::warn("Could not delete session {}: not found!", session_id);
    }
}

void OnUpgrade(uWS::HttpResponse<false>* http_response, uWS::HttpRequest* http_request, struct us_socket_context_t* context) {
    string address;
    auto ip_header = http_request->getHeader("x-forwarded-for");
    if (!ip_header.empty()) {
        address = ip_header;
    } else {
        address = IPAsText(http_response->getRemoteAddress());
    }

    // Check if there's a token to be matched
    if (!auth_token.empty()) {
        string req_token = GetAuthToken(http_request);

        if (!req_token.empty()) {
            if (req_token != auth_token) {
                spdlog::error("Incorrect auth token supplied! Closing WebSocket connection");
                http_response->close();
                return;
            }
        } else {
            spdlog::error("No auth token supplied! Closing WebSocket connection");
            http_response->close();
            return;
        }
    }

    session_number++;
    // protect against overflow
    session_number = max(session_number, 1u);

    http_response->template upgrade<PerSocketData>({session_number, address}, //
        http_request->getHeader("sec-websocket-key"),                         //
        http_request->getHeader("sec-websocket-protocol"),                    //
        http_request->getHeader("sec-websocket-extensions"),                  //
        context);
}

// Called on connection. Creates session objects and assigns UUID to it
void OnConnect(uWS::WebSocket<false, true>* ws) {
    auto socket_data = static_cast<PerSocketData*>(ws->getUserData());
    if (!socket_data) {
        spdlog::error("Error handling WebSocket connection: Socket data does not exist");
        return;
    }

    uint32_t session_id = socket_data->session_id;
    string address = socket_data->address;

    // get the uWebsockets loop
    auto* loop = uWS::Loop::get();

    // create a Session
    sessions[session_id] = new Session(
        ws, loop, session_id, address, settings.top_level_folder, settings.starting_folder, file_list_handler, settings.grpc_port);

    if (carta_grpc_service) {
        carta_grpc_service->AddSession(sessions[session_id]);
    }

    sessions[session_id]->IncreaseRefCount();

    spdlog::info("Session {} [{}] Connected. Num sessions: {}", session_id, address, Session::NumberOfSessions());
}

// Called on disconnect. Cleans up sessions. In future, we may want to delay this (in case of unintentional disconnects)
void OnDisconnect(uWS::WebSocket<false, true>* ws, int code, std::string_view message) {
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

void OnDrain(uWS::WebSocket<false, true>* ws) {
    uint32_t session_id = static_cast<PerSocketData*>(ws->getUserData())->session_id;
    Session* session = sessions[session_id];
    if (session) {
        spdlog::debug("Draining WebSocket backpressure: client {} [{}]. Remaining buffered amount: {} (bytes).", session->GetId(),
            session->GetAddress(), ws->getBufferedAmount());
    } else {
        spdlog::debug("Draining WebSocket backpressure: unknown client. Remaining buffered amount: {} (bytes).", ws->getBufferedAmount());
    }
}

// Forward message requests to session callbacks after parsing message into relevant ProtoBuf message
void OnMessage(uWS::WebSocket<false, true>* ws, std::string_view sv_message, uWS::OpCode op_code) {
    uint32_t session_id = static_cast<PerSocketData*>(ws->getUserData())->session_id;
    Session* session = sessions[session_id];
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
                case CARTA::EventType::FILE_LIST_REQUEST: {
                    CARTA::FileListRequest message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnFileListRequest(message, head.request_id);
                    } else {
                        spdlog::warn("Bad FILE_LIST_REQUEST message!");
                    }
                    break;
                }
                case CARTA::EventType::OPEN_FILE: {
                    CARTA::OpenFile message;
                    if (message.ParseFromArray(event_buf, event_length)) {
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
                case CARTA::EventType::REGION_LIST_REQUEST: {
                    CARTA::RegionListRequest message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnRegionListRequest(message, head.request_id);
                    } else {
                        spdlog::warn("Bad REGION_LIST_REQUEST message!");
                    }
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
                case CARTA::EventType::CATALOG_LIST_REQUEST: {
                    CARTA::CatalogListRequest message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnCatalogFileList(message, head.request_id);
                    } else {
                        spdlog::warn("Bad CATALOG_LIST_REQUEST message!");
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
            if ((settings.idle_session_wait_time > 0) && (dt.count() >= settings.idle_session_wait_time)) {
                spdlog::warn("Client {} has been idle for {} seconds. Disconnecting..", session->GetId(), dt.count());
                ws->close();
            } else {
                ws->send("PONG", uWS::OpCode::TEXT);
            }
        }
    }
}

void GrpcSilentLogger(gpr_log_func_args*) {}

extern void gpr_default_log(gpr_log_func_args* args);

int StartGrpcService(int grpc_port) {
    // Silence grpc error log
    gpr_set_log_function(GrpcSilentLogger);

    // Set up address buffer
    std::string server_address = fmt::format("0.0.0.0:{}", grpc_port);

    // Build grpc service
    grpc::ServerBuilder builder;
    // BuildAndStart will populate this with the desired port if binding succeeds or 0 if it fails
    int selected_port(-1);
    // Listen on the given address without any authentication mechanism.
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials(), &selected_port);

    // Register and start carta grpc server
    carta_grpc_service = std::unique_ptr<CartaGrpcService>(new CartaGrpcService());
    builder.RegisterService(carta_grpc_service.get());
    // By default ports can be reused; we don't want this
    builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
    carta_grpc_server = builder.BuildAndStart();

    if (selected_port > 0) { // available port found
        spdlog::info("CARTA gRPC service available at 0.0.0.0:{}", selected_port);
        // Turn logging back on
        gpr_set_log_function(gpr_default_log);
        return 0;
    } else {
        spdlog::critical("CARTA gRPC service failed to start. Could not bind to port {}. Aborting.", grpc_port);
        return 1;
    }
}

void ExitBackend(int s) {
    spdlog::info("Exiting backend.");
    if (carta_grpc_server) {
        carta_grpc_server->Shutdown();
    }
    FlushLogFile();
    exit(0);
}

// Entry point. Parses command line arguments and starts server listening
int main(int argc, char* argv[]) {
    try {
        // set up interrupt signal handler
        struct sigaction sig_handler;
        sig_handler.sa_handler = ExitBackend;
        sigemptyset(&sig_handler.sa_mask);
        sig_handler.sa_flags = 0;
        sigaction(SIGINT, &sig_handler, nullptr);

        settings = ProgramSettings(argc, argv);

        if (settings.help || settings.version) {
            exit(0);
        }

        InitLogger(settings.no_log, settings.verbosity, settings.log_performance, settings.log_protocol_messages);

        if (settings.wait_time >= 0) {
            Session::SetExitTimeout(settings.wait_time);
        }

        if (settings.init_wait_time >= 0) {
            Session::SetInitExitTimeout(settings.init_wait_time);
        }

        std::string executable_path;
        bool have_executable_path(FindExecutablePath(executable_path));

        if (!have_executable_path) {
            spdlog::warn("Could not determine the full path to the backend executable.");
            executable_path = "carta_backend";
        }

        spdlog::info("{}: Version {}", executable_path, VERSION_ID);

        if (!CheckFolderPaths(settings.top_level_folder, settings.starting_folder)) {
            FlushLogFile();
            return 1;
        }

        if (!settings.debug_no_auth) {
            auto env_entry = getenv("CARTA_AUTH_TOKEN");
            if (env_entry) {
                auth_token = env_entry;
            } else {
                uuid_t token;
                char token_string[37];
                uuid_generate_random(token);
                uuid_unparse(token, token_string);
                auth_token += token_string;
            }
        }

        tbb::task_scheduler_init task_scheduler(TBB_TASK_THREAD_COUNT);
        carta::ThreadManager::SetThreadLimit(settings.omp_thread_count);

        // One FileListHandler works for all sessions.
        file_list_handler = new FileListHandler(settings.top_level_folder, settings.starting_folder);

        // Start grpc service for scripting client
        if (settings.grpc_port >= 0) {
            int grpc_status = StartGrpcService(settings.grpc_port);
            if (grpc_status) {
                FlushLogFile();
                return 1;
            }
        }

        // Init curl
        curl_global_init(CURL_GLOBAL_ALL);

        session_number = 0;
        auto app = uWS::App();

        if (!settings.no_http) {
            fs::path frontend_path;

            if (!settings.frontend_folder.empty()) {
                frontend_path = settings.frontend_folder;
            } else if (have_executable_path) {
                fs::path executable_parent = fs::path(executable_path).parent_path();
                frontend_path = executable_parent / CARTA_DEFAULT_FRONTEND_FOLDER;
            } else {
                spdlog::warn(
                    "Failed to determine the default location of the CARTA frontend. Please specify a custom location using the "
                    "frontend_folder argument.");
            }

            if (!frontend_path.empty()) {
                http_server = new SimpleFrontendServer(frontend_path, auth_token);
                if (http_server->CanServeFrontend()) {
                    http_server->RegisterRoutes(app);
                } else {
                    spdlog::warn("Failed to host the CARTA frontend. Please specify a custom location using the frontend_folder argument.");
                }
            }
        }

        bool port_ok(false);

        if (settings.port < 0) {
            settings.port = DEFAULT_SOCKET_PORT;
            int num_listen_retries(0);
            while (!port_ok) {
                if (num_listen_retries > MAX_SOCKET_PORT_TRIALS) {
                    spdlog::error("Unable to listen on the port range {}-{}!", DEFAULT_SOCKET_PORT, settings.port - 1);
                    break;
                }
                app.listen(settings.host, settings.port, LIBUS_LISTEN_EXCLUSIVE_PORT, [&](auto* token) {
                    if (token) {
                        port_ok = true;
                    } else {
                        spdlog::warn("Port {} is already in use. Trying next port.", settings.port);
                        ++settings.port;
                        ++num_listen_retries;
                    }
                });
            }
        } else {
            // If the user specifies a valid port, we should not try other ports
            app.listen(settings.host, settings.port, LIBUS_LISTEN_EXCLUSIVE_PORT, [&](auto* token) {
                if (token) {
                    port_ok = true;
                } else {
                    spdlog::error("Could not listen on port {}!\n", settings.port);
                }
            });
        }

        if (port_ok) {
            string start_info = fmt::format("Listening on port {} with top level folder {}, starting folder {}", settings.port,
                settings.top_level_folder, settings.starting_folder);
            if (settings.omp_thread_count > 0) {
                start_info += fmt::format(", and {} OpenMP worker threads", settings.omp_thread_count);
            } else {
                start_info += fmt::format(". The number of OpenMP worker threads will be handled automatically.");
            }
            spdlog::info(start_info);
            if (http_server && http_server->CanServeFrontend()) {
                string default_host_string = settings.host;
                if (default_host_string.empty() || default_host_string == "0.0.0.0") {
                    auto server_ip_entry = getenv("SERVER_IP");
                    if (server_ip_entry) {
                        default_host_string = server_ip_entry;
                    } else {
                        default_host_string = "localhost";
                    }
                }
                string frontend_url = fmt::format("http://{}:{}", default_host_string, settings.port);
                string query_url;
                if (!auth_token.empty()) {
                    query_url += fmt::format("/?token={}", auth_token);
                }
                if (!settings.files.empty()) {
                    // TODO: Handle multiple files once the frontend supports this
                    query_url += query_url.empty() ? "/?" : "&";
                    query_url += fmt::format("file={}", curl_easy_escape(nullptr, settings.files[0].c_str(), 0));
                }

                if (!query_url.empty()) {
                    frontend_url += query_url;
                }
                if (!settings.no_browser) {
#if defined(__APPLE__)
                    string open_command = "open";
#else
                    string open_command = "xdg-open";
#endif
                    auto open_result = system(fmt::format("{} \"{}\"", open_command, frontend_url).c_str());
                    if (open_result) {
                        spdlog::warn("Failed to open the default browser automatically.");
                    }
                }
                spdlog::info("CARTA is accessible at {}", frontend_url);
            }

            app.ws<PerSocketData>("/*", (uWS::App::WebSocketBehavior){.compression = uWS::DEDICATED_COMPRESSOR_256KB,
                                            .maxPayloadLength = 256 * 1024 * 1024,
                                            .maxBackpressure = MAX_BACKPRESSURE,
                                            .upgrade = OnUpgrade,
                                            .open = OnConnect,
                                            .message = OnMessage,
                                            .drain = OnDrain,
                                            .close = OnDisconnect})
                .run();
        }
    } catch (exception& e) {
        spdlog::critical("{}", e.what());
        FlushLogFile();
        return 1;
    } catch (...) {
        spdlog::critical("Unknown error");
        FlushLogFile();
        return 1;
    }

    FlushLogFile();
    return 0;
}
