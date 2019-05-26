#include <iostream>
#include <mutex>
#include <thread>
#include <tuple>
#include <vector>

#include <fmt/format.h>
#include <signal.h>
#include <tbb/concurrent_queue.h>
#include <tbb/concurrent_unordered_map.h>
#include <tbb/task.h>
#include <tbb/task_scheduler_init.h>
#include <uWS/uWS.h>

#include <casacore/casa/Inputs/Input.h>
#include <casacore/casa/OS/HostInfo.h>

#include "EventHeader.h"
#include "FileListHandler.h"
#include "FileSettings.h"
#include "OnMessageTask.h"
#include "Session.h"
#include "Util.h"

using namespace std;

// key is current folder
unordered_map<string, vector<string>> permissions_map;

// file list handler for the file browser
FileListHandler* file_list_handler;

uint32_t session_number;
uWS::Hub websocket_hub;

// command-line arguments
string root_folder("/"), base_folder("."), version_id("1.1");
bool verbose, use_permissions;

// Called on connection. Creates session objects and assigns UUID and API keys to it
void OnConnect(uWS::WebSocket<uWS::SERVER>* ws, uWS::HttpRequest http_request) {
    session_number++;
    // protect against overflow
    session_number = max(session_number, 1u);

    uS::Async* outgoing = new uS::Async(websocket_hub.getLoop());

    Session* session;

    outgoing->start([](uS::Async* async) -> void {
        Session* current_session = ((Session*)async->getData());
        current_session->SendPendingMessages();
    });

    session = new Session(ws, session_number, root_folder, outgoing, file_list_handler, verbose);

    ws->setUserData(session);
    session->IncreaseRefCount();
    outgoing->setData(session);

    Log(session_number, "Client {} [{}] Connected. Num sessions: {}", session_number, ws->getAddress().address,
        Session::NumberOfSessions());
}

// Called on disconnect. Cleans up sessions. In future, we may want to delay this (in case of unintentional disconnects)
void OnDisconnect(uWS::WebSocket<uWS::SERVER>* ws, int code, char* message, size_t length) {
    Session* session = (Session*)ws->getUserData();

    if (session) {
        auto uuid = session->_id;
        session->DisconnectCalled();
        Log(uuid, "Client {} [{}] Disconnected. Remaining sessions: {}", uuid, ws->getAddress().address, Session::NumberOfSessions());
        if (!session->DecreaseRefCount()) {
            delete session;
            ws->setUserData(nullptr);
        }
    } else {
        cerr << "Warning: OnDisconnect called with no Session object.\n";
    }
}

// Forward message requests to session callbacks after parsing message into relevant ProtoBuf message
void OnMessage(uWS::WebSocket<uWS::SERVER>* ws, char* raw_message, size_t length, uWS::OpCode op_code) {
    Session* session = (Session*)ws->getUserData();
    if (!session) {
        fmt::print("Missing session!\n");
        return;
    }

    if (op_code == uWS::OpCode::BINARY) {
        if (length > sizeof(carta::EventHeader)) {
            carta::EventHeader head = *reinterpret_cast<carta::EventHeader*>(raw_message);
            char* event_buf = raw_message + sizeof(carta::EventHeader);
            int event_length = length - sizeof(carta::EventHeader);
            OnMessageTask* tsk = nullptr;

            switch (head.type) {
                case CARTA::EventType::REGISTER_VIEWER: {
                    CARTA::RegisterViewer message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnRegisterViewer(message, head.icd_version, head.request_id);
                    }
                    break;
                }
                case CARTA::EventType::SET_IMAGE_CHANNELS: {
                    CARTA::SetImageChannels message;
                    message.ParseFromArray(event_buf, event_length);
                    session->ImageChannelLock();
                    if (!session->ImageChannelTaskTestAndSet()) {
                        tsk = new (tbb::task::allocate_root(session->context()))
                            SetImageChannelsTask(session, make_pair(message, head.request_id));
                    } else {
                        // has its own queue to keep channels in order during animation
                        session->AddToSetChannelQueue(message, head.request_id);
                    }
                    session->ImageChannelUnlock();
                    break;
                }
                case CARTA::EventType::SET_IMAGE_VIEW: {
                    CARTA::SetImageView message;
                    message.ParseFromArray(event_buf, event_length);
                    session->AddViewSetting(message, head.request_id);
                    tsk = new (tbb::task::allocate_root(session->context())) SetImageViewTask(session, message.file_id());
                    break;
                }
                case CARTA::EventType::SET_CURSOR: {
                    CARTA::SetCursor message;
                    message.ParseFromArray(event_buf, event_length);
                    session->AddCursorSetting(message, head.request_id);
                    tsk = new (tbb::task::allocate_root(session->context())) SetCursorTask(session, message.file_id());
                    break;
                }
                case CARTA::EventType::SET_HISTOGRAM_REQUIREMENTS: {
                    CARTA::SetHistogramRequirements message;
                    message.ParseFromArray(event_buf, event_length);
                    if (message.histograms_size() == 0) {
                        session->CancelSetHistRequirements();
                    } else {
                        tsk = new (tbb::task::allocate_root(session->HistoContext()))
                            SetHistogramRequirementsTask(session, head, event_length, event_buf);
                    }
                    break;
                }
                case CARTA::EventType::CLOSE_FILE: {
                    CARTA::CloseFile message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->CheckCancelAnimationOnFileClose(message.file_id());
                        session->_file_settings.ClearSettings(message.file_id());
                        session->OnCloseFile(message);
                    }
                    break;
                }
                case CARTA::EventType::START_ANIMATION: {
                    CARTA::StartAnimation message;
                    message.ParseFromArray(event_buf, event_length);
                    session->cancelExistingAnimation();
                    session->BuildAnimationObject(message, head.request_id);
                    tsk = new (tbb::task::allocate_root(session->AnimationContext())) AnimationTask(session);
                    break;
                }
                case CARTA::EventType::STOP_ANIMATION: {
                    CARTA::StopAnimation message;
                    message.ParseFromArray(event_buf, event_length);
                    session->StopAnimation(message.file_id(), message.end_frame());
                    break;
                }
                case CARTA::EventType::ANIMATION_FLOW_CONTROL: {
                    CARTA::AnimationFlowControl message;
                    message.ParseFromArray(event_buf, event_length);
                    session->HandleAnimationFlowControlEvt(message);
                    break;
                }
                case CARTA::EventType::FILE_INFO_REQUEST: {
                    CARTA::FileInfoRequest message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnFileInfoRequest(message, head.request_id);
                    }
                    break;
                }
                default: {
                    tsk = new (tbb::task::allocate_root(session->context())) MultiMessageTask(session, head, event_length, event_buf);
                }
            }

            if (tsk)
                tbb::task::enqueue(*tsk);
        }
    } else if (op_code == uWS::OpCode::TEXT) {
        if (strncmp(raw_message, "PING", 4) == 0) {
            ws->send("PONG");
        }
    }
}

void ExitBackend(int s) {
    fmt::print("Exiting backend.\n");
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
        int thread_count(tbb::task_scheduler_init::default_num_threads());
        { // get values then let Input go out of scope
            casacore::Input inp;
            int tmp;
            inp.version(version_id);
            inp.create("verbose", "False", "display verbose logging", "Bool");
            inp.create("permissions", "False", "use a permissions file for determining access", "Bool");
            inp.create("port", to_string(port), "set server port", "Int");
            inp.create("threads", to_string(thread_count), "set thread pool count", "Int");
            inp.create("base", base_folder, "set folder for data files", "String");
            inp.create("root", root_folder, "set top-level folder for data files", "String");
            inp.create("exit_after", to_string(tmp), "number of seconds to stay alive afer last sessions exists", "Int");
            inp.readArguments(argc, argv);

            verbose = inp.getBool("verbose");
            use_permissions = inp.getBool("permissions");
            port = inp.getInt("port");
            thread_count = inp.getInt("threads");
            base_folder = inp.getString("base");
            root_folder = inp.getString("root");

            if (inp.getInt("exit_after")) {
                tmp = inp.getInt("exit_after");
                Session::SetExitTimeout(tmp);
            }
        }

        if (!CheckRootBaseFolders(root_folder, base_folder)) {
            return 1;
        }

        // Construct task scheduler, permissions
        tbb::task_scheduler_init task_scheduler(thread_count);
        if (use_permissions) {
            ReadPermissions("permissions.txt", permissions_map);
        }

        // One FileListHandler works for all sessions.
        file_list_handler = new FileListHandler(permissions_map, use_permissions, root_folder, base_folder);

        session_number = 0;

        websocket_hub.onMessage(&OnMessage);
        websocket_hub.onConnection(&OnConnect);
        websocket_hub.onDisconnection(&OnDisconnect);
        if (websocket_hub.listen(port)) {
            fmt::print("Listening on port {} with root folder {}, base folder {}, and {} threads in thread pool\n", port, root_folder,
                base_folder, thread_count);
            websocket_hub.run();
        } else {
            fmt::print("Error listening on port {}\n", port);
            return 1;
        }
    } catch (exception& e) {
        fmt::print("Error: {}\n", e.what());
        return 1;
    } catch (...) {
        fmt::print("Unknown error\n");
        return 1;
    }
    return 0;
}
