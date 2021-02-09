/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <climits>
#include <iostream>
#include <thread>
#include <tuple>
#include <vector>

#include <App.h>
#include <curl/curl.h>
#include <signal.h>
#include <tbb/task.h>
#include <tbb/task_scheduler_init.h>
#include <uuid/uuid.h>

#include <casacore/casa/Inputs/Input.h>
#include <casacore/casa/OS/HostInfo.h>

#include "EventHeader.h"
#include "FileList/FileListHandler.h"
#include "FileSettings.h"
#include "GrpcServer/CartaGrpcService.h"
#include "Logger/Logger.h"
#include "OnMessageTask.h"
#include "Session.h"
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

// command-line arguments
static string root_folder("/"), base_folder(".");
// token to validate incoming WS connection header against
static string auth_token = "";
static int grpc_port(-1);

// Sessions map
std::unordered_map<uint32_t, Session*> sessions;

// Apply ws->getUserData and return one of these
struct PerSocketData {
    uint32_t session_id;
    string address;
};

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
        string req_token;
        // First try the cookie auth token
        auto cookie_header = http_request->getHeader("cookie");
        if (!cookie_header.empty()) {
            auto cookie_auth_value = GetAuthTokenFromCookie(string(cookie_header));
            if (!cookie_auth_value.empty()) {
                req_token = cookie_auth_value;
            }
        }
        // Then try the auth header
        if (req_token.empty()) {
            req_token = http_request->getHeader("carta-auth-token");
        }

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
    uint32_t session_id = static_cast<PerSocketData*>(ws->getUserData())->session_id;
    string address = static_cast<PerSocketData*>(ws->getUserData())->address;

    // get the uWebsockets loop
    auto* loop = uWS::Loop::get();

    // create a Session
    sessions[session_id] = new Session(ws, loop, session_id, address, root_folder, base_folder, file_list_handler, grpc_port);

    if (carta_grpc_service) {
        carta_grpc_service->AddSession(sessions[session_id]);
    }

    sessions[session_id]->IncreaseRefCount();

    spdlog::info("Client {} [{}] Connected. Num sessions: {}", session_id, address, Session::NumberOfSessions());
}

// Called on disconnect. Cleans up sessions. In future, we may want to delay this (in case of unintentional disconnects)
void OnDisconnect(uWS::WebSocket<false, true>* ws, int code, std::string_view message) {
    // Skip server-forced disconnects
    if (code == 4003) {
        return;
    }

    // Get the Session object
    uint32_t session_id = static_cast<PerSocketData*>(ws->getUserData())->session_id;
    Session* session = sessions[session_id];

    if (session) {
        auto uuid = session->GetId();
        auto address = session->GetAddress();
        session->DisconnectCalled();
        spdlog::info("Client {} [{}] Disconnected. Remaining sessions: {}", uuid, address, Session::NumberOfSessions());
        if (carta_grpc_service) {
            carta_grpc_service->RemoveSession(session);
        }
        if (!session->DecreaseRefCount()) {
            delete session;
            sessions.erase(session_id);
        } else {
            spdlog::warn("Session reference count ({}) is not 0 while on disconnection!", session->DecreaseRefCount());
        }
    } else {
        spdlog::warn("OnDisconnect called with no Session object!");
    }

    // Close the websockets
    ws->close();
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
            carta::EventHeader head = *reinterpret_cast<const carta::EventHeader*>(sv_message.data());
            const char* event_buf = sv_message.data() + sizeof(carta::EventHeader);
            int event_length = sv_message.length() - sizeof(carta::EventHeader);
            OnMessageTask* tsk = nullptr;
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
            ws->send("PONG", uWS::OpCode::TEXT);
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
    spdlog::default_logger()->flush(); // flush the log file while exiting backend
    exit(0);
}

bool FindExecutablePath(std::string& path) {
    char path_buffer[PATH_MAX + 1];
#ifdef __APPLE__
    uint32_t len = sizeof(path_buffer);

    if (_NSGetExecutablePath(path_buffer, &len) != 0) {
        return false;
    }
#else
    const int len = int(readlink("/proc/self/exe", path_buffer, PATH_MAX));

    if (len == -1) {
        return false;
    }

    path_buffer[len] = 0;
#endif
    path = path_buffer;
    return true;
}

// Entry point. Parses command line arguments and starts server listening
int main(int argc, const char* argv[]) {
    try {
        // set up interrupt signal handler
        struct sigaction sig_handler;
        sig_handler.sa_handler = ExitBackend;
        sigemptyset(&sig_handler.sa_mask);
        sig_handler.sa_flags = 0;
        sigaction(SIGINT, &sig_handler, nullptr);

        // define and get input arguments
        int port(-1);
        int omp_thread_count = OMP_THREAD_COUNT;
        int thread_count = THREAD_COUNT;
        string frontend_folder;
        string host;
        bool no_http = false;
        bool debug_no_auth = false;
        bool no_browser = false;
        bool no_log = false;
        int verbosity = 4;

        { // get values then let Input go out of scope
            casacore::Input inp;
            inp.create("verbosity", to_string(verbosity),
                "display verbose logging from level (0: off, 1: critical, 2: error, 3: warning, 4: info, 5: debug, 6: trace)", "Int");
            inp.create("no_log", "False", "Do not output to a log file", "Bool");
            inp.create("no_http", "False", "disable CARTA frontend HTTP server", "Bool");
            inp.create("debug_no_auth", "False", "accept all incoming WebSocket connections (insecure, use with caution!)", "Bool");
            inp.create("no_browser", "False", "prevent the frontend from automatically opening in the default browser on startup", "Bool");
            inp.create("host", host, "only listen on the specified interface (IP address or hostname)", "String");
            inp.create("port", to_string(port), "set port on which to host frontend files and accept WebSocket connections", "Int");
            inp.create("grpc_port", to_string(grpc_port), "set grpc server port", "Int");
            inp.create("threads", to_string(thread_count), "set thread count for handling incoming messages", "Int");
            inp.create("omp_threads", to_string(omp_thread_count), "set OpenMP thread pool count. To handle automatically, use -1", "Int");
            inp.create("base", base_folder, "set folder for data files", "String");
            inp.create("root", root_folder, "set top-level folder for data files", "String");
            inp.create("frontend_folder", frontend_folder, "set folder to serve frontend files from", "String");
            inp.create("exit_after", "", "number of seconds to stay alive after last sessions exists", "Int");
            inp.create("init_exit_after", "", "number of seconds to stay alive at start if no clients connect", "Int");
            inp.readArguments(argc, argv);

            verbosity = inp.getInt("verbosity");
            no_log = inp.getBool("no_log");
            no_http = inp.getBool("no_http");
            debug_no_auth = inp.getBool("debug_no_auth");
            no_browser = inp.getBool("no_browser");
            port = inp.getInt("port");
            host = inp.getString("host");
            grpc_port = inp.getInt("grpc_port");
            omp_thread_count = inp.getInt("omp_threads");
            thread_count = inp.getInt("threads");
            base_folder = inp.getString("base");
            root_folder = inp.getString("root");
            frontend_folder = inp.getString("frontend_folder");

            if (!inp.getString("exit_after").empty()) {
                int wait_time = inp.getInt("exit_after");
                Session::SetExitTimeout(wait_time);
            }
            if (!inp.getString("init_exit_after").empty()) {
                int init_wait_time = inp.getInt("init_exit_after");
                Session::SetInitExitTimeout(init_wait_time);
            }

            InitLogger(no_log, verbosity);
        }

        std::string executable_path;
        bool have_executable_path(FindExecutablePath(executable_path));

        if (!have_executable_path) {
            spdlog::warn("Could not determine the full path to the backend executable.");
            executable_path = "carta_backend";
        }

        spdlog::info("{}: Version {}", executable_path, VERSION_ID);

        if (!CheckRootBaseFolders(root_folder, base_folder)) {
            return 1;
        }

        if (!debug_no_auth) {
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

        tbb::task_scheduler_init task_scheduler(thread_count);
        carta::ThreadManager::SetThreadLimit(omp_thread_count);

        // One FileListHandler works for all sessions.
        file_list_handler = new FileListHandler(root_folder, base_folder);

        // Start grpc service for scripting client
        if (grpc_port >= 0) {
            int grpc_status = StartGrpcService(grpc_port);
            if (grpc_status) {
                return 1;
            }
        }

        // Init curl
        curl_global_init(CURL_GLOBAL_ALL);

        session_number = 0;
        auto app = uWS::App();

        if (!no_http) {
            fs::path frontend_path;

            if (!frontend_folder.empty()) {
                frontend_path = frontend_folder;
            } else if (have_executable_path) {
                fs::path executable_parent = fs::path(executable_path).parent_path();
                frontend_path = executable_parent / "../share/carta/frontend";
            } else {
                spdlog::warn(
                    "Failed to determine the default location of the CARTA frontend. Please specify a custom location using the "
                    "frontend_folder argument.");
            }

            if (!frontend_path.empty()) {
                http_server = new SimpleFrontendServer(frontend_path);
                if (http_server->CanServeFrontend()) {
                    app.get("/*", [&](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
                        if (http_server && http_server->CanServeFrontend()) {
                            http_server->HandleRequest(res, req);
                        }
                    });
                } else {
                    spdlog::warn("Failed to host the CARTA frontend. Please specify a custom location using the frontend_folder argument.");
                }
            }
        }

        host = host.empty() ? "0.0.0.0" : host;
        port = (port < 0) ? DEFAULT_SOCKET_PORT : port;
        bool port_ok(false);

        if (port == DEFAULT_SOCKET_PORT) {
            int num_listen_retries(0);
            while (!port_ok) {
                if (num_listen_retries > MAX_SOCKET_PORT_TRIALS) {
                    spdlog::error("Unable to listen on the port range {}-{}!", DEFAULT_SOCKET_PORT, port - 1);
                    break;
                }
                app.listen(host, port, LIBUS_LISTEN_EXCLUSIVE_PORT, [&](auto* token) {
                    if (token) {
                        port_ok = true;
                    } else {
                        spdlog::warn("Port {} is already in use. Trying next port.", port);
                        ++port;
                        ++num_listen_retries;
                    }
                });
            }
        } else {
            // If the user specifies a port, we should not try other ports
            app.listen(host, port, LIBUS_LISTEN_EXCLUSIVE_PORT, [&](auto* token) {
                if (token) {
                    port_ok = true;
                } else {
                    spdlog::error("Could not listen on port {}!\n", port);
                }
            });
        }

        if (port_ok) {
            string start_info = fmt::format("Listening on port {} with root folder {}, base folder {}", port, root_folder, base_folder);
            if (omp_thread_count > 0) {
                start_info += fmt::format(", and {} OpenMP worker threads", omp_thread_count);
            } else {
                start_info += fmt::format(". The number of OpenMP worker threads will be handled automatically.");
            }
            spdlog::info(start_info);
            if (http_server && http_server->CanServeFrontend()) {
                string default_host_string = host;
                if (host.empty() || host == "0.0.0.0") {
                    auto server_ip_entry = getenv("SERVER_IP");
                    if (server_ip_entry) {
                        default_host_string = server_ip_entry;
                    } else {
                        default_host_string = "localhost";
                    }
                }
                string frontend_url = fmt::format("http://{}:{}", default_host_string, port);
                if (!auth_token.empty()) {
                    frontend_url += fmt::format("/?token={}", auth_token);
                }
                if (!no_browser) {
#if defined(__APPLE__)
                    string open_command = "open";
#else
                    string open_command = "xdg-open";
#endif
                    auto open_result = system(fmt::format("{} {}", open_command, frontend_url).c_str());
                    if (open_result) {
                        spdlog::warn("Failed to open the default browser automatically.");
                    }
                }
                spdlog::info("CARTA is accessible at {}", frontend_url);
            }

            app.ws<PerSocketData>("/*", (uWS::App::WebSocketBehavior){.compression = uWS::DEDICATED_COMPRESSOR_256KB,
                                            .upgrade = OnUpgrade,
                                            .open = OnConnect,
                                            .message = OnMessage,
                                            .close = OnDisconnect})
                .run();
        }
    } catch (exception& e) {
        spdlog::critical("{}", e.what());
        return 1;
    } catch (...) {
        spdlog::critical("Unknown error");
        return 1;
    }

    return 0;
}
