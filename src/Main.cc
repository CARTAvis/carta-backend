/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <thread>
#include <vector>

#include <curl/curl.h>
#include <signal.h>

#include "FileList/FileListHandler.h"
#include "FileSettings.h"
#include "GrpcServer/CartaGrpcService.h"
#include "Logger/Logger.h"
#include "SessionManager/ProgramSettings.h"
#include "SessionManager/SessionManager.h"
#include "SessionManager/WebBrowser.h"
#include "SimpleFrontendServer/SimpleFrontendServer.h"
#include "Threading.h"
#include "Util/App.h"
#include "Util/FileSystem.h"
#include "Util/Token.h"

#define TBB_TASK_THREAD_COUNT 3

using namespace std;

// Entry point. Parses command line arguments and starts server listening
int main(int argc, char* argv[]) {
    ProgramSettings settings;

    std::shared_ptr<FileListHandler> file_list_handler;
    std::unique_ptr<SimpleFrontendServer> http_server;
    std::shared_ptr<GrpcManager> grpc_manager;
    std::shared_ptr<SessionManager> session_manager;

    try {
        // set up interrupt signal handler
        struct sigaction sig_handler;

        sig_handler.sa_handler = [](int s) {
            spdlog::info("Exiting backend.");
            ThreadManager::ExitEventHandlingThreads();
            FlushLogFile();
            exit(0);
        };

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
                auth_token = NewAuthToken();
            }
        }

        carta::ThreadManager::StartEventHandlingThreads(settings.event_thread_count);
        carta::ThreadManager::SetThreadLimit(settings.omp_thread_count);

        // One FileListHandler works for all sessions.
        file_list_handler = std::make_shared<FileListHandler>(settings.top_level_folder, settings.starting_folder);

        // Start gRPC server for scripting client.
        if (settings.grpc_port >= 0) {
            std::string grpc_token = "";
            bool fixed_grpc_token(false);

            if (!settings.debug_no_auth) {
                auto env_entry = getenv("CARTA_GRPC_TOKEN");

                if (env_entry) {
                    grpc_token = env_entry;
                    fixed_grpc_token = true;
                } else {
                    grpc_token = NewAuthToken();
                }
            }

            grpc_manager = std::make_shared<GrpcManager>(settings.grpc_port, grpc_token);

            if (grpc_manager->Listening()) {
                spdlog::info("CARTA gRPC service available at 0.0.0.0:{}", settings.grpc_port);
                if (!fixed_grpc_token && !settings.debug_no_auth) {
                    spdlog::info("CARTA gRPC token: {}", grpc_token);
                }
            } else {
                spdlog::critical("CARTA gRPC service failed to start. Could not bind to port {}. Aborting.", settings.grpc_port);
                FlushLogFile();
                return 1;
            }
        } else {
            grpc_manager = std::make_shared<GrpcManager>(); // dummy manager
        }

        // Init curl
        curl_global_init(CURL_GLOBAL_ALL);

        // Session manager
        session_manager = make_shared<SessionManager>(settings, auth_token, file_list_handler, grpc_manager->Service());

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

                auto file_query_url = SimpleFrontendServer::GetFileUrlString(settings.files);
                if (!file_query_url.empty()) {
                    query_url += (query_url.empty() ? "/?" : "&") + file_query_url;
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
