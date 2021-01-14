/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <iostream>
#include <thread>
#include <tuple>
#include <vector>

#include <omp.h>

#include <App.h>
#include <curl/curl.h>
#include <fmt/format.h>
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
#include "OnMessageTask.h"
#include "Session.h"
#include "SimpleFrontendServer/SimpleFrontendServer.h"
#include "Util.h"

using namespace std;

namespace CARTA {
int global_thread_count = 0;
}
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
static bool verbose;
static bool perflog;
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
        // First try the URL parameter
        auto req_token = http_request->getParameter(0);
        if (req_token.empty()) {
            req_token = http_request->getHeader("carta-auth-token");
        }
        if (!req_token.empty()) {
            string token_header_value(req_token);
            if (token_header_value != auth_token) {
                fmt::print("Header auth failed!\n");
                http_response->close();
                return;
            }
        } else {
            fmt::print("Header auth failed!\n");
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
    sessions[session_id] =
        new Session(ws, loop, session_id, address, root_folder, base_folder, file_list_handler, verbose, perflog, grpc_port);

    if (carta_grpc_service) {
        carta_grpc_service->AddSession(sessions[session_id]);
    }

    sessions[session_id]->IncreaseRefCount();

    carta::Log(session_id, "Client {} [{}] Connected. Num sessions: {}", session_id, address, Session::NumberOfSessions());
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
        carta::Log(uuid, "Client {} [{}] Disconnected. Remaining sessions: {}", uuid, address, Session::NumberOfSessions());
        if (carta_grpc_service) {
            carta_grpc_service->RemoveSession(session);
        }
        if (!session->DecreaseRefCount()) {
            delete session;
            sessions.erase(session_id);
        } else {
            fmt::print("Warning: Session reference count ({}) is not 0 while on disconnection!\n", session->DecreaseRefCount());
        }
    } else {
        fmt::print("Warning: OnDisconnect called with no Session object!\n");
    }

    // Close the websockets
    ws->close();
}

// Forward message requests to session callbacks after parsing message into relevant ProtoBuf message
void OnMessage(uWS::WebSocket<false, true>* ws, std::string_view sv_message, uWS::OpCode op_code) {
    uint32_t session_id = static_cast<PerSocketData*>(ws->getUserData())->session_id;
    Session* session = sessions[session_id];
    if (!session) {
        fmt::print("Missing session!\n");
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
                        fmt::print("Bad REGISTER_VIEWER message!\n");
                    }
                    break;
                }
                case CARTA::EventType::RESUME_SESSION: {
                    CARTA::ResumeSession message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnResumeSession(message, head.request_id);
                    } else {
                        fmt::print("Bad RESUME_SESSION message!\n");
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
                        fmt::print("Bad SET_IMAGE_CHANNELS message!\n");
                    }
                    break;
                }
                case CARTA::EventType::SET_CURSOR: {
                    CARTA::SetCursor message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->AddCursorSetting(message, head.request_id);
                        tsk = new (tbb::task::allocate_root(session->Context())) SetCursorTask(session, message.file_id());
                    } else {
                        fmt::print("Bad SET_CURSOR message!\n");
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
                        fmt::print("Bad SET_HISTOGRAM_REQUIREMENTS message!\n");
                    }
                    break;
                }
                case CARTA::EventType::CLOSE_FILE: {
                    CARTA::CloseFile message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnCloseFile(message);
                    } else {
                        fmt::print("Bad CLOSE_FILE message!\n");
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
                        fmt::print("Bad START_ANIMATION message!\n");
                    }
                    break;
                }
                case CARTA::EventType::STOP_ANIMATION: {
                    CARTA::StopAnimation message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->StopAnimation(message.file_id(), message.end_frame());
                    } else {
                        fmt::print("Bad STOP_ANIMATION message!\n");
                    }
                    break;
                }
                case CARTA::EventType::ANIMATION_FLOW_CONTROL: {
                    CARTA::AnimationFlowControl message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->HandleAnimationFlowControlEvt(message);
                    } else {
                        fmt::print("Bad ANIMATION_FLOW_CONTROL message!\n");
                    }
                    break;
                }
                case CARTA::EventType::FILE_INFO_REQUEST: {
                    CARTA::FileInfoRequest message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnFileInfoRequest(message, head.request_id);
                    } else {
                        fmt::print("Bad FILE_INFO_REQUEST message!\n");
                    }
                    break;
                }
                case CARTA::EventType::FILE_LIST_REQUEST: {
                    CARTA::FileListRequest message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnFileListRequest(message, head.request_id);
                    } else {
                        fmt::print("Bad FILE_LIST_REQUEST message!\n");
                    }
                    break;
                }
                case CARTA::EventType::OPEN_FILE: {
                    CARTA::OpenFile message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnOpenFile(message, head.request_id);
                    } else {
                        fmt::print("Bad OPEN_FILE message!\n");
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
                        fmt::print("Bad REGION_LIST_REQUEST message!\n");
                    }
                    break;
                }
                case CARTA::EventType::REGION_FILE_INFO_REQUEST: {
                    CARTA::RegionFileInfoRequest message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnRegionFileInfoRequest(message, head.request_id);
                    } else {
                        fmt::print("Bad REGION_FILE_INFO_REQUEST message!\n");
                    }
                    break;
                }
                case CARTA::EventType::IMPORT_REGION: {
                    CARTA::ImportRegion message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnImportRegion(message, head.request_id);
                    } else {
                        fmt::print("Bad IMPORT_REGION message!\n");
                    }
                    break;
                }
                case CARTA::EventType::EXPORT_REGION: {
                    CARTA::ExportRegion message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnExportRegion(message, head.request_id);
                    } else {
                        fmt::print("Bad EXPORT_REGION message!\n");
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
                        fmt::print("Bad SCRIPTING_RESPONSE message!\n");
                    }
                    break;
                }
                case CARTA::EventType::SET_REGION: {
                    CARTA::SetRegion message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnSetRegion(message, head.request_id);
                    } else {
                        fmt::print("Bad SET_REGION message!\n");
                    }
                    break;
                }
                case CARTA::EventType::REMOVE_REGION: {
                    CARTA::RemoveRegion message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnRemoveRegion(message);
                    } else {
                        fmt::print("Bad REMOVE_REGION message!\n");
                    }
                    break;
                }
                case CARTA::EventType::SET_SPECTRAL_REQUIREMENTS: {
                    CARTA::SetSpectralRequirements message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnSetSpectralRequirements(message);
                    } else {
                        fmt::print("Bad SET_SPECTRAL_REQUIREMENTS message!\n");
                    }
                    break;
                }
                case CARTA::EventType::CATALOG_LIST_REQUEST: {
                    CARTA::CatalogListRequest message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnCatalogFileList(message, head.request_id);
                    } else {
                        fmt::print("Bad CATALOG_LIST_REQUEST message!\n");
                    }
                    break;
                }
                case CARTA::EventType::CATALOG_FILE_INFO_REQUEST: {
                    CARTA::CatalogFileInfoRequest message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnCatalogFileInfo(message, head.request_id);
                    } else {
                        fmt::print("Bad CATALOG_FILE_INFO_REQUEST message!\n");
                    }
                    break;
                }
                case CARTA::EventType::OPEN_CATALOG_FILE: {
                    CARTA::OpenCatalogFile message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnOpenCatalogFile(message, head.request_id);
                    } else {
                        fmt::print("Bad OPEN_CATALOG_FILE message!\n");
                    }
                    break;
                }
                case CARTA::EventType::CLOSE_CATALOG_FILE: {
                    CARTA::CloseCatalogFile message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnCloseCatalogFile(message);
                    } else {
                        fmt::print("Bad CLOSE_CATALOG_FILE message!\n");
                    }
                    break;
                }
                case CARTA::EventType::CATALOG_FILTER_REQUEST: {
                    CARTA::CatalogFilterRequest message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnCatalogFilter(message, head.request_id);
                    } else {
                        fmt::print("Bad CLOSE_CATALOG_FILE message!\n");
                    }
                    break;
                }
                case CARTA::EventType::STOP_MOMENT_CALC: {
                    CARTA::StopMomentCalc message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnStopMomentCalc(message);
                    } else {
                        fmt::print("Bad STOP_MOMENT_CALC message!\n");
                    }
                    break;
                }
                case CARTA::EventType::SAVE_FILE: {
                    CARTA::SaveFile message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnSaveFile(message, head.request_id);
                    } else {
                        fmt::print("Bad SAVE_FILE message!\n");
                    }
                    break;
                }
                case CARTA::EventType::SPECTRAL_LINE_REQUEST: {
                    CARTA::SpectralLineRequest message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        tsk =
                            new (tbb::task::allocate_root(session->Context())) OnSpectralLineRequestTask(session, message, head.request_id);
                    } else {
                        fmt::print("Bad SPECTRAL_LINE_REQUEST message!\n");
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
    carta_grpc_service = std::unique_ptr<CartaGrpcService>(new CartaGrpcService(verbose));
    builder.RegisterService(carta_grpc_service.get());
    // By default ports can be reused; we don't want this
    builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
    carta_grpc_server = builder.BuildAndStart();

    if (selected_port > 0) { // available port found
        fmt::print("CARTA gRPC service available at 0.0.0.0:{}\n", selected_port);
        // Turn logging back on
        gpr_set_log_function(gpr_default_log);
        return 0;
    } else {
        fmt::print("CARTA gRPC service failed to start. Could not bind to port {}. Aborting.\n", grpc_port);
        return 1;
    }
}

void ExitBackend(int s) {
    fmt::print("Exiting backend.\n");
    if (carta_grpc_server) {
        carta_grpc_server->Shutdown();
    }
    exit(0);
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
        int port(3002);
        int thread_count = TBB_THREAD_COUNT;
        int omp_thread_count = OMP_THREAD_COUNT;
        string http_root_folder;
        bool no_http = false;
        bool no_token = false;
        bool no_browser = false;

        { // get values then let Input go out of scope
            casacore::Input inp;
            inp.version(VERSION_ID);
            inp.create("verbose", "False", "display verbose logging", "Bool");
            inp.create("perflog", "False", "display performance logging", "Bool");
            inp.create("no_http", "False", "disable CARTA frontend HTTP server", "Bool");
            inp.create("no_token", "False", "disable token validation on connection", "Bool");
            inp.create("no_browser", "False", "prevent the frontend from automatically opening in the default browser on startup", "Bool");
            inp.create("port", to_string(port), "set server port", "Int");
            inp.create("grpc_port", to_string(grpc_port), "set grpc server port", "Int");
            inp.create("threads", to_string(thread_count), "set thread pool count", "Int");
            inp.create("omp_threads", to_string(omp_thread_count), "set OMP thread pool count", "Int");
            inp.create("base", base_folder, "set folder for data files", "String");
            inp.create("root", root_folder, "set top-level folder for data files", "String");
            inp.create("http_root", http_root_folder, "set folder to serve frontend file from", "String");
            inp.create("exit_after", "", "number of seconds to stay alive after last sessions exists", "Int");

            inp.create("init_exit_after", "", "number of seconds to stay alive at start if no clents connect", "Int");
            inp.readArguments(argc, argv);

            verbose = inp.getBool("verbose");
            perflog = inp.getBool("perflog");
            no_http = inp.getBool("no_http");
            no_token = inp.getBool("no_token");
            no_browser = inp.getBool("no_browser");
            port = inp.getInt("port");
            grpc_port = inp.getInt("grpc_port");
            thread_count = inp.getInt("threads");
            omp_thread_count = inp.getInt("omp_threads");
            base_folder = inp.getString("base");
            root_folder = inp.getString("root");

            if (!inp.getString("exit_after").empty()) {
                int wait_time = inp.getInt("exit_after");
                Session::SetExitTimeout(wait_time);
            }
            if (!inp.getString("init_exit_after").empty()) {
                int init_wait_time = inp.getInt("init_exit_after");
                Session::SetInitExitTimeout(init_wait_time);
            }
        }

        if (!CheckRootBaseFolders(root_folder, base_folder)) {
            return 1;
        }

        if (!no_token) {
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

        omp_set_num_threads(omp_thread_count);
        CARTA::global_thread_count = omp_thread_count;

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
            http_server = new SimpleFrontendServer(http_root_folder);
            if (http_server->CanServeFrontend()) {
                app.get("/*", [&](uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
                    if (http_server && http_server->CanServeFrontend()) {
                        http_server->HandleRequest(res, req);
                    }
                });

                string frontend_url = fmt::format("http://localhost:{}", port);
                if (!auth_token.empty()) {
                    frontend_url += fmt::format("?socketUrl=ws://localhost:{}/token/{}", port, auth_token);
                }
                if (!no_browser) {
#if defined(__APPLE__)
                    string open_command = "open";
#else
                    string open_command = "xdg-open";
#endif
                        auto open_result = system(fmt::format("{} {}", open_command, frontend_url).c_str());
                    if (open_result) {
                        fmt::print("Failed to open the default browser automatically.\n");
                    }
                }
                fmt::print("CARTA is accessible at {}\n", frontend_url);
            }
        }

        string ws_pattern;
        if (no_token) {
            ws_pattern = "/*";
        } else {
            ws_pattern = "/token/:token";
        }

        app.ws<PerSocketData>(ws_pattern, (uWS::App::WebSocketBehavior){.compression = uWS::SHARED_COMPRESSOR,
                                              .upgrade = OnUpgrade,
                                              .open = OnConnect,
                                              .message = OnMessage,
                                              .close = OnDisconnect})
            .listen(port,
                [=](auto* token) {
                    if (token) {
                        fmt::print(
                            "Listening on port {} with root folder {}, base folder {}, {} threads in worker thread pool and {} OMP "
                            "threads\n",
                            port, root_folder, base_folder, thread_count, omp_thread_count);
                    } else {
                        fmt::print("Error listening on port {}\n", port);
                    }
                })
            .run();
    } catch (exception& e) {
        fmt::print("Error: {}\n", e.what());
        return 1;
    } catch (...) {
        fmt::print("Unknown error\n");
        return 1;
    }

    return 0;
}
