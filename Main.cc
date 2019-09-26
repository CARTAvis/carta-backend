

#if _AUTH_SERVER_
#include <jsoncpp/json/json.h>
#include <jsoncpp/json/value.h>
#include "DBConnect.h"
#endif

#include <iostream>
#include <tuple>
#include <vector>

#include <fmt/format.h>
#include <signal.h>
#include <tbb/task_scheduler_init.h>
#include <uWS/uWS.h>

#include <casacore/casa/Inputs/Input.h>
#include <casacore/casa/OS/HostInfo.h>

#include "FileListHandler.h"
#include "FileSettings.h"
#include "MessageHandler.h"
#include "Session.h"

using namespace std;

namespace CARTA {
int global_thread_count = 0;
}
// key is current folder
unordered_map<string, vector<string>> permissions_map;

// file list handler for the file browser
static FileListHandler* file_list_handler;

static uint32_t session_number;
static uWS::Hub websocket_hub;

// command-line arguments
static string root_folder("/"), base_folder(".");
static bool verbose, use_permissions, use_mongodb;
namespace CARTA {
string token;
string mongo_db_contact_string;
} // namespace CARTA

// Called on connection. Creates session objects and assigns UUID and API keys to it
void OnConnect(uWS::WebSocket<uWS::SERVER>* ws, uWS::HttpRequest http_request) {
    // Check for authorization token
    if (!CARTA::token.empty()) {
        string expected_auth_header = fmt::format("CARTA-Authorization={}", CARTA::token);
        auto cookie_header = http_request.getHeader("cookie");
        string auth_header_string(cookie_header.value, cookie_header.valueLength);
        if (auth_header_string.find(expected_auth_header) == string::npos) {
            Log(0, "Invalid authorization token header, closing socket");
            ws->close(403, "Invalid authorization token");
            return;
        }
    }
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
    // Skip server-forced disconnects
    if (code == 4003) {
        return;
    }

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
        MessageHandler(session, raw_message, length);
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
        int thread_count(tbb::task_scheduler_init::default_num_threads());
        { // get values then let Input go out of scope
            casacore::Input inp;
            string json_fname;
            inp.version(VERSION_ID);
            inp.create("verbose", "False", "display verbose logging", "Bool");
            inp.create("permissions", "False", "use a permissions file for determining access", "Bool");
            inp.create("token", CARTA::token, "only accept connections with this authorization token", "String");
            inp.create("port", to_string(port), "set server port", "Int");
            inp.create("threads", to_string(thread_count), "set thread pool count", "Int");
            inp.create("base", base_folder, "set folder for data files", "String");
            inp.create("root", root_folder, "set top-level folder for data files", "String");
            inp.create("exit_after", "", "number of seconds to stay alive after last sessions exists", "Int");
            inp.create("init_exit_after", "", "number of seconds to stay alive at start if no clents connect", "Int");
            inp.create("read_json_file", json_fname, "read in json file with secure token", "String");
            inp.create("use_mongodb", "False", "use mongo db", "Bool");
            inp.readArguments(argc, argv);

            verbose = inp.getBool("verbose");
            use_mongodb = inp.getBool("use_mongodb");
            use_permissions = inp.getBool("permissions");
            port = inp.getInt("port");
            thread_count = inp.getInt("threads");
            base_folder = inp.getString("base");
            root_folder = inp.getString("root");
            CARTA::token = inp.getString("token");
            CARTA::mongo_db_contact_string = "mongodb://localhost:27017/";
            if (use_mongodb) {
#if _AUTH_SERVER_
                ConnectToMongoDB();
#else
                std::cerr << "Not configured to use MongoDB" << std::endl;
                exit(1);
#endif
            }
            bool has_exit_after_arg = inp.getString("exit_after").size();
            if (has_exit_after_arg) {
                int wait_time = inp.getInt("exit_after");
                Session::SetExitTimeout(wait_time);
            }
            bool has_init_exit_after_arg = inp.getString("init_exit_after").size();
            if (has_init_exit_after_arg) {
                int init_wait_time = inp.getInt("init_exit_after");
                Session::SetInitExitTimeout(init_wait_time);
            }
            bool should_read_json_file = inp.getString("read_json_file").size();
            if (should_read_json_file) {
                json_fname = inp.getString("read_json_file");
                ReadJsonFile(json_fname);
            }
        }

        if (!CheckRootBaseFolders(root_folder, base_folder)) {
            return 1;
        }

        // Construct task scheduler, permissions
        tbb::task_scheduler_init task_scheduler(thread_count);
        CARTA::global_thread_count = thread_count;
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
