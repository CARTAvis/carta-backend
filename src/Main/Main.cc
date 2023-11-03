/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <thread>
#include <vector>

#include <signal.h>

#include "FileList/FileListHandler.h"
#include "HttpServer/HttpServer.h"
#include "Logger/CartaLogSink.h"
#include "Logger/Logger.h"
#include "ProgramSettings.h"
#include "Session/SessionManager.h"
#include "ThreadingManager/ThreadingManager.h"
#include "Util/App.h"
#include "Util/FileSystem.h"
#include "Util/Token.h"
#include "WebBrowser.h"

#include <casacore/casa/Logging/LogIO.h>
#include <casacore/casa/Logging/NullLogSink.h>

// Entry point. Parses command line arguments and starts server listening
int main(int argc, char* argv[]) {
    std::shared_ptr<FileListHandler> file_list_handler;
    std::unique_ptr<HttpServer> http_server;
    std::shared_ptr<SessionManager> session_manager;

    try {
        // set up interrupt signal handler
        struct sigaction sig_handler;

        sig_handler.sa_handler = [](int s) {
            spdlog::info("Exiting backend.");
            ThreadManager::ExitEventHandlingThreads();
            carta::logger::FlushLogFile();
            exit(0);
        };

        sigemptyset(&sig_handler.sa_mask);
        sig_handler.sa_flags = 0;
        sigaction(SIGINT, &sig_handler, nullptr);

        // Main
        carta::ProgramSettings settings(argc, argv);

        if (settings.help || settings.version) {
            exit(0);
        }

        carta::logger::InitLogger(
            settings.no_log, settings.verbosity, settings.log_performance, settings.log_protocol_messages, settings.user_directory);
        settings.FlushMessages(); // flush log messages produced during Program Settings setup

        // Send casacore log messages (global and local) to sink.
        // CartaLogSink sends messages to spdlog, NullLogSink discards messages.
        casacore::LogSinkInterface* carta_log_sink(nullptr);
        switch (settings.verbosity) {
            case 0:
                carta_log_sink = new casacore::NullLogSink();
                break;
            case 1:
            case 2:
                carta_log_sink = new CartaLogSink(casacore::LogMessage::SEVERE);
                break;
            case 3:
                carta_log_sink = new CartaLogSink(casacore::LogMessage::WARN);
                break;
            case 4:
            case 5:
            default:
                carta_log_sink = new CartaLogSink(casacore::LogMessage::NORMAL);
        }
        casacore::LogSink log_sink(carta_log_sink->filter(), casacore::CountedPtr<casacore::LogSinkInterface>(carta_log_sink));
        casacore::LogSink::globalSink(carta_log_sink);
        casacore::LogIO casacore_log(log_sink);

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
            carta::logger::FlushLogFile();
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

        if (settings.controller_deployment) {
            Session::SetControllerDeploymentFlag(settings.controller_deployment);
        }

        carta::ThreadManager::StartEventHandlingThreads(settings.event_thread_count);
        carta::ThreadManager::SetThreadLimit(settings.omp_thread_count);

        // One FileListHandler works for all sessions.
        file_list_handler = std::make_shared<FileListHandler>(settings.top_level_folder, settings.starting_folder);

        // Session manager
        session_manager = make_shared<SessionManager>(settings, auth_token, file_list_handler);
        carta::OnMessageTask::SetSessionManager(session_manager);

        // HTTP server
        if (!settings.no_frontend || !settings.no_database || settings.enable_scripting) {
            fs::path frontend_path;

            if (!settings.no_frontend) {
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
            }

            http_server =
                std::make_unique<HttpServer>(session_manager, frontend_path, settings.user_directory, auth_token, settings.read_only_mode,
                    !settings.no_frontend, !settings.no_database, settings.enable_scripting, !settings.no_runtime_config);
            http_server->RegisterRoutes();

            if (!settings.no_frontend && !http_server->CanServeFrontend()) {
                spdlog::warn("Failed to host the CARTA frontend. Please specify a custom location using the frontend_folder argument.");
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

            if (http_server) {
                string default_host_string = settings.host;
                if (default_host_string.empty() || default_host_string == "0.0.0.0") {
                    auto server_ip_entry = getenv("SERVER_IP");
                    if (server_ip_entry) {
                        default_host_string = server_ip_entry;
                    } else {
                        default_host_string = "localhost";
                    }
                }

                string base_url = fmt::format("http://{}:{}", default_host_string, port);

                if (!settings.no_frontend && http_server->CanServeFrontend()) {
                    string frontend_url = base_url;

                    string query_url;
                    if (!auth_token.empty()) {
                        query_url += fmt::format("/?token={}", auth_token);
                    }

                    auto file_query_url = HttpServer::GetFileUrlString(settings.files);
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

                if (!settings.no_database) {
                    string database_url = base_url + "/api/database/...";
                    spdlog::debug("The CARTA database API is accessible at {}", database_url);
                }

                if (settings.enable_scripting) {
                    string scripting_url = base_url + "/api/scripting/action";
                    spdlog::debug("To use the CARTA scripting interface, send POST requests to {}", scripting_url);
                }
            }

            session_manager->RunApp();
        }
    } catch (exception& e) {
        spdlog::critical("{}", e.what());
        carta::logger::FlushLogFile();
        return 1;
    } catch (...) {
        spdlog::critical("Unknown error");
        carta::logger::FlushLogFile();
        return 1;
    }

    carta::logger::FlushLogFile();
    return 0;
}
