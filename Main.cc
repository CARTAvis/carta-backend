#if _AUTH_SERVER_
#include <jsoncpp/json/json.h>
#include <jsoncpp/json/value.h>

#include "DBConnect.h"
#endif

#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <tuple>
#include <vector>

#include <omp.h>

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
#include "FileList/FileListHandler.h"
#include "FileSettings.h"
#include "OnMessageTask.h"
#include "Session.h"
#include "Util.h"

using namespace std;

namespace CARTA {
int global_thread_count = 0;
const char* mongo_default_uri = "mongodb://localhost:27017/";
} // namespace CARTA
// key is current folder
unordered_map<string, vector<string>> permissions_map;

// file list handler for the file browser
static FileListHandler* file_list_handler;

static uint32_t session_number;
static uWS::Hub websocket_hub(uWS::PERMESSAGE_DEFLATE);

// command-line arguments
static string root_folder("/"), base_folder(".");
static bool verbose, use_permissions, use_mongodb;
namespace CARTA {
string token;
string mongo_db_contact_string;
} // namespace CARTA

void GetMongoURIString(string& uri) {
    uri = CARTA::mongo_db_contact_string;
}

// Called on connection. Creates session objects and assigns UUID and API keys to it
void OnConnect(uWS::WebSocket<uWS::SERVER>* ws, uWS::HttpRequest http_request) {
    // Check for authorization token
    if (!CARTA::token.empty()) {
        string expected_auth_header = fmt::format("CARTA-Authorization={}", CARTA::token);
        auto cookie_header = http_request.getHeader("cookie");
        string auth_header_string(cookie_header.value, cookie_header.valueLength);
        if (auth_header_string.find(expected_auth_header) == string::npos) {
            PrintLog(0, "Invalid authorization token header, closing socket");
            ws->close(403, "Invalid authorization token");
            return;
        }
    }
    session_number++;
    // protect against overflow
    session_number = max(session_number, 1u);

    uS::Async* outgoing = new uS::Async(websocket_hub.getLoop());

    outgoing->start([](uS::Async* async) -> void {
        Session* current_session = ((Session*)async->getData());
        current_session->SendPendingMessages();
    });

    Session* session = new Session(ws, session_number, root_folder, outgoing, file_list_handler, verbose);

    ws->setUserData(session);
    session->IncreaseRefCount();
    outgoing->setData(session);

    PrintLog(session_number, "Client {} [{}] Connected. Num sessions: {}", session_number, ws->getAddress().address,
        Session::NumberOfSessions());
}

// Called on disconnect. Cleans up sessions. In future, we may want to delay this (in case of unintentional disconnects)
void OnDisconnect(uWS::WebSocket<uWS::SERVER>* ws, int code, char* message, size_t length) {
    // Skip server-forced disconnects
    if (code == 4003) {
        return;
    }

    Session* session = (Session*)ws->getUserData();

    if (session) {
        auto uuid = session->_id;
        session->DisconnectCalled();
        PrintLog(uuid, "Client {} [{}] Disconnected. Remaining sessions: {}", uuid, ws->getAddress().address, Session::NumberOfSessions());
        if (!session->DecreaseRefCount()) {
            delete session;
            ws->setUserData(nullptr);
        }
    } else {
        cerr << "Warning: OnDisconnect called with no Session object.\n";
    }
}

void OnError(void* user) {
    switch ((long)user) {
        case 3:
            cerr << "Client emitted error on connection timeout (non-SSL)" << endl;
            break;
        case 5:
            cerr << "Client emitted error on connection timeout (SSL)" << endl;
            break;
        default:
            cerr << "FAILURE: " << user << " should not emit error!" << endl;
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
                case CARTA::EventType::SET_USER_PREFERENCES: {
                    CARTA::SetUserPreferences message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnSetUserPreferences(message, head.request_id);
                    } else {
                        fmt::print("Bad SET_USER_PREFERENCES message!\n");
                    }
                    break;
                }
                case CARTA::EventType::SET_USER_LAYOUT: {
                    CARTA::SetUserLayout message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnSetUserLayout(message, head.request_id);
                    } else {
                        fmt::print("Bad SET_USER_LAYOUT message!\n");
                    }
                    break;
                }
                case CARTA::EventType::SET_CONTOUR_PARAMETERS: {
                    CARTA::SetContourParameters message;
                    message.ParseFromArray(event_buf, event_length);
                    tsk = new (tbb::task::allocate_root(session->Context())) OnSetContourParametersTask(session, message);
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
                case CARTA::EventType::MOMENT_REQUEST: {
                    CARTA::MomentRequest message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                        session->OnMomentRequest(message, head.request_id);
                    } else {
                        fmt::print("Bad MOMENT_REQUEST message!\n");
                    }
                    break;
                }
                case CARTA::EventType::STOP_MOMENT_CALC: {
                    CARTA::StopMomentCalc message;
                    if (message.ParseFromArray(event_buf, event_length)) {
                    } else {
                        fmt::print("Bad STOP_MOMENT_CALC message!\n");
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
        if (strncmp(raw_message, "PING", 4) == 0) {
            ws->send("PONG");
        }
    }
}

void ExitBackend(int s) {
    fmt::print("Exiting backend.\n");
    exit(0);
}

void ReadJsonFile(const string& fname) {
#if _AUTH_SERVER_
    std::ifstream config_file(fname);
    if (!config_file.is_open()) {
        std::cerr << "Failed to open config file " << fname << std::endl;
        exit(1);
    }
    Json::Value json_config;
    Json::Reader reader;
    reader.parse(config_file, json_config);
    CARTA::token = json_config["token"].asString();
    if (CARTA::token.empty()) {
        std::cerr << "Bad config file.\n";
        exit(1);
    }
#else
    std::cerr << "Not configured to use JSON." << std::endl;
#endif
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
        { // get values then let Input go out of scope
            casacore::Input inp;
            string json_fname;
            inp.version(VERSION_ID);
            inp.create("verbose", "False", "display verbose logging", "Bool");
            inp.create("permissions", "False", "use a permissions file for determining access", "Bool");
            inp.create("token", CARTA::token, "only accept connections with this authorization token", "String");
            inp.create("port", to_string(port), "set server port", "Int");
            inp.create("threads", to_string(thread_count), "set thread pool count", "Int");
            inp.create("omp_threads", to_string(omp_thread_count), "set OMP thread pool count", "Int");
            inp.create("base", base_folder, "set folder for data files", "String");
            inp.create("root", root_folder, "set top-level folder for data files", "String");
            inp.create("exit_after", "", "number of seconds to stay alive after last sessions exists", "Int");
            inp.create("init_exit_after", "", "number of seconds to stay alive at start if no clents connect", "Int");
            inp.create("read_json_file", json_fname, "read in json file with secure token", "String");
            inp.create("use_mongodb", "False", "use mongo db", "Bool");
            inp.create("mongo_uri", CARTA::mongo_db_contact_string, "URI to connect to MongoDB", "String");
            inp.readArguments(argc, argv);

            verbose = inp.getBool("verbose");
            use_mongodb = inp.getBool("use_mongodb");
            use_permissions = inp.getBool("permissions");
            port = inp.getInt("port");
            thread_count = inp.getInt("threads");
            omp_thread_count = inp.getInt("omp_threads");
            base_folder = inp.getString("base");
            root_folder = inp.getString("root");
            CARTA::token = inp.getString("token");
            CARTA::mongo_db_contact_string = CARTA::mongo_default_uri;
            if (!inp.getString("mongo_uri").empty()) {
                CARTA::mongo_db_contact_string = inp.getString("mongo_uri");
            }
            if (use_mongodb) {
#if _AUTH_SERVER_
                ConnectToMongoDB();
#else
                std::cerr << "Not configured to use MongoDB" << std::endl;
                exit(1);
#endif
            }
            if (!inp.getString("exit_after").empty()) {
                int wait_time = inp.getInt("exit_after");
                Session::SetExitTimeout(wait_time);
            }
            if (!inp.getString("init_exit_after").empty()) {
                int init_wait_time = inp.getInt("init_exit_after");
                Session::SetInitExitTimeout(init_wait_time);
            }
            if (!inp.getString("read_json_file").empty()) {
                json_fname = inp.getString("read_json_file");
                ReadJsonFile(json_fname);
            }
        }

        if (!CheckRootBaseFolders(root_folder, base_folder)) {
            return 1;
        }

        tbb::task_scheduler_init task_scheduler(thread_count);
        omp_set_num_threads(omp_thread_count);
        CARTA::global_thread_count = omp_thread_count;
        if (use_permissions) {
            ReadPermissions("permissions.txt", permissions_map);
        }

        // One FileListHandler works for all sessions.
        file_list_handler = new FileListHandler(permissions_map, use_permissions, root_folder, base_folder);

        session_number = 0;

        websocket_hub.onMessage(&OnMessage);
        websocket_hub.onConnection(&OnConnect);
        websocket_hub.onDisconnection(&OnDisconnect);
        websocket_hub.onError(&OnError);

        if (websocket_hub.listen(port)) {
            fmt::print("Listening on port {} with root folder {}, base folder {}, {} threads in worker thread pool and {} OMP threads\n",
                port, root_folder, base_folder, thread_count, omp_thread_count);
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
