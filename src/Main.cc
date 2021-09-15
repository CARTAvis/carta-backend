/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <limits> // for numeric limits
#include <thread>
#include <tuple>
#include <vector>

#include <curl/curl.h>
#include <signal.h>
#include <tbb/task.h>
#include <tbb/task_scheduler_init.h>
#include <uuid/uuid.h>

#include "FileList/FileListHandler.h"
#include "FileSettings.h"
#include "GrpcServer/CartaGrpcService.h"
#include "Logger/Logger.h"
#include "SessionManager/ProgramSettings.h"
#include "SessionManager/SessionManager.h"
#include "SessionManager/WebBrowser.h"
#include "SimpleFrontendServer/SimpleFrontendServer.h"
#include "Threading.h"

#define TBB_TASK_THREAD_COUNT 3

using namespace std;

ProgramSettings settings;

// file list handler for the file browser
static std::shared_ptr<FileListHandler> file_list_handler;
static std::unique_ptr<SimpleFrontendServer> http_server;

// grpc server for scripting client
static std::shared_ptr<CartaGrpcService> carta_grpc_service;
static std::unique_ptr<grpc::Server> carta_grpc_server;

static std::shared_ptr<SessionManager> session_manager;

void GrpcSilentLogger(gpr_log_func_args*) {}

extern void gpr_default_log(gpr_log_func_args* args);

int StartGrpcService() {
    // Silence grpc error log
    gpr_set_log_function(GrpcSilentLogger);

    // Set up address buffer
    std::string server_address = fmt::format("0.0.0.0:{}", settings.grpc_port);

    // Build grpc service
    grpc::ServerBuilder builder;
    // BuildAndStart will populate this with the desired port if binding succeeds or 0 if it fails
    int selected_port(-1);
    // Listen on the given address without any authentication mechanism.
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials(), &selected_port);

    std::string grpc_token = "";
    bool fixed_grpc_token(false);

    if (!settings.debug_no_auth) {
        auto env_entry = getenv("CARTA_GRPC_TOKEN");

        if (env_entry) {
            grpc_token = env_entry;
            fixed_grpc_token = true;
        } else {
            uuid_t token;
            char token_string[37];
            uuid_generate_random(token);
            uuid_unparse(token, token_string);
            grpc_token += token_string;
        }
    }

    // Register and start carta grpc server
    carta_grpc_service = std::make_shared<CartaGrpcService>(grpc_token);
    builder.RegisterService(carta_grpc_service.get());
    // By default ports can be reused; we don't want this
    builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
    carta_grpc_server = builder.BuildAndStart();

    if (selected_port > 0) { // available port found
        spdlog::info("CARTA gRPC service available at 0.0.0.0:{}", selected_port);
        if (!fixed_grpc_token && !settings.debug_no_auth) {
            spdlog::info("CARTA gRPC token: {}", grpc_token);
        }
        // Turn logging back on
        gpr_set_log_function(gpr_default_log);
        return 0;
    } else {
        spdlog::critical("CARTA gRPC service failed to start. Could not bind to port {}. Aborting.", settings.grpc_port);
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

        // Settings
        carta::ProgramSettings settings(argc, argv);

        if (settings.help || settings.version) {
            exit(0);
        }

        InitLogger(settings.no_log, settings.verbosity, settings.log_performance, settings.log_protocol_messages, settings.user_directory);
        settings.FlushMessages(); // flush log messages produced during Program Settings setup

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

        std::string auth_token = "";

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
        file_list_handler = std::make_shared<FileListHandler>(settings.top_level_folder, settings.starting_folder);

        // Start gRPC server for scripting client.
        std::shared_ptr<CartaGrpcService> carta_grpc_service;
        std::unique_ptr<grpc::Server> carta_grpc_server;
        if (settings.grpc_port >= 0) {
            int grpc_status = StartGrpcService();
            if (grpc_status) {
                FlushLogFile();
                return 1;
            }
        }

        // Init curl
        curl_global_init(CURL_GLOBAL_ALL);

        // Session manager
        session_manager = make_shared<SessionManager>(settings, auth_token, file_list_handler, carta_grpc_service);

        // HTTP server
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
                http_server =
                    std::make_unique<SimpleFrontendServer>(frontend_path, settings.user_directory, auth_token, settings.read_only_mode);
                if (http_server->CanServeFrontend()) {
                    http_server->RegisterRoutes(session_manager->App());
                } else {
                    spdlog::warn("Failed to host the CARTA frontend. Please specify a custom location using the frontend_folder argument.");
                }
            }
        }

        int port(-1);
        session_manager->Listen(settings.host, settings.port, DEFAULT_SOCKET_PORT, port);

        if (port > -1) {
            string start_info = fmt::format("Listening on port {} with top level folder {}, starting folder {}", port,
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
                string frontend_url = fmt::format("http://{}:{}", default_host_string, port);
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
                    WebBrowser wb(frontend_url, settings.browser);
                    if (!wb.Status()) {
                        spdlog::warn(wb.Error());
                    }
                }
                spdlog::info("CARTA is accessible at {}", frontend_url);
            }

            session_manager->RunApp();
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
