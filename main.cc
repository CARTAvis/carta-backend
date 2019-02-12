#include "AnimationQueue.h"
#include "FileSettings.h"
#include "OnMessageTask.h"
#include "Session.h"
#include "util.h"

#include <casacore/casa/OS/HostInfo.h>
#include <casacore/casa/Inputs/Input.h>
#include <fmt/format.h>
#include <tbb/concurrent_queue.h>
#include <tbb/concurrent_unordered_map.h>
#include <tbb/task_scheduler_init.h>
#include <uWS/uWS.h>

#include <fstream>
#include <iostream>
#include <regex>
#include <tuple>
#include <vector>
#include <signal.h>

#define MAX_THREADS 4

using namespace std;

// key is uuid:
using key_type = string;
unordered_map<key_type, Session*> sessions;
unordered_map<key_type, carta::AnimationQueue*> animationQueues;
unordered_map<key_type, carta::FileSettings*> fileSettings;
// msgQueue holds eventName, requestId, message
unordered_map<key_type, tbb::concurrent_queue<tuple<string,uint32_t,vector<char>>>*> msgQueues;

// key is current folder
unordered_map<string, vector<string>> permissionsMap;

int sessionNumber;
uWS::Hub wsHub;

// command-line arguments
string rootFolder("/"), baseFolder("./"), version_id("1.0.1");
bool verbose, usePermissions;

bool checkRootBaseFolders(std::string& root, std::string& base) {
    if (root=="base" && base == "root") {
        fmt::print("ERROR: Must set root or base directory.\n");
        fmt::print("Exiting carta.\n");
        return false;
    }

    if (root=="base") root = base;
    if (base=="root") base = root;

    // check root
    casacore::File rootFolder(root);
    if (!(rootFolder.exists() && rootFolder.isDirectory(true) && 
          rootFolder.isReadable() && rootFolder.isExecutable())) {
        fmt::print("ERROR: Invalid root directory, does not exist or is not a readable directory.\n");
        fmt::print("Exiting carta.\n");
        return false;
    }
    if (root.compare("/") != 0) {
        // absolute path: resolve symlinks, relative paths, env vars e.g. $HOME
        root = rootFolder.path().resolvedName();
    }
    // check base
    casacore::File baseFolder(base);
    if (!(baseFolder.exists() && baseFolder.isDirectory(true) && 
          baseFolder.isReadable() && baseFolder.isExecutable())) {
        fmt::print("ERROR: Invalid base directory, does not exist or is not a readable directory.\n");
        fmt::print("Exiting carta.\n");
        return false;
    }
    if (base.compare("/") != 0) {
        // absolute path: resolve symlinks, relative paths, env vars e.g. $HOME
        base = baseFolder.path().resolvedName();
    }
    // check if base is same as or subdir of root
    if (base != root) {
        bool isSubdirectory(false);
        casacore::Path basePath(base);
	casacore::String parentString(basePath.dirName()), rootString(root);
        while (parentString != rootString) {  // navigate up directory tree
            basePath = casacore::Path(parentString);
	    parentString = basePath.dirName();
	    if (parentString == rootString) {
                isSubdirectory = true;
		break;
            } else if (parentString == "/") {
                break;
            }
        }
	if (!isSubdirectory) {
            fmt::print("ERROR: Base must be a subdirectory of root. Exiting carta.\n");
	    return false;
        }
    }
    return true;
}

// Reads a permissions file to determine which API keys are required to access various subdirectories
void readPermissions(string filename) {
    ifstream permissionsFile(filename);
    if (permissionsFile.good()) {
        fmt::print("Reading permissions file\n");
        string line;
        regex commentRegex("\\s*#.*");
        regex folderRegex("\\s*(\\S+):\\s*");
        regex keyRegex("\\s*(\\S{4,}|\\*)\\s*");
        string currentFolder;
        while (getline(permissionsFile, line)) {
            smatch matches;
            if (regex_match(line, commentRegex)) {
                continue;
            } else if (regex_match(line, matches, folderRegex) && matches.size() == 2) {
                currentFolder = matches[1].str();
            } else if (currentFolder.length() && regex_match(line, matches, keyRegex) && matches.size() == 2) {
                string key = matches[1].str();
                permissionsMap[currentFolder].push_back(key);
            }
        }
    } else {
        fmt::print("Missing permissions file\n");
    }
}

// Called on connection. Creates session objects and assigns UUID and API keys to it
void onConnect(uWS::WebSocket<uWS::SERVER>* ws, uWS::HttpRequest httpRequest) {
    string uuidstr = fmt::format("{}{}", ++sessionNumber,
        casacore::Int(casacore::HostInfo::secondsFrom1970()));
    ws->setUserData(new string(uuidstr));
    auto &uuid = *((string*)ws->getUserData());

    uS::Async *outgoing = new uS::Async(wsHub.getLoop());
    outgoing->setData(&uuid);
    outgoing->start(
        [](uS::Async *async) -> void {
            auto uuid = *((string*)async->getData());
            sessions[uuid]->sendPendingMessages();
        });

    sessions[uuid] = new Session(ws, uuid, permissionsMap, usePermissions, rootFolder, baseFolder, outgoing, verbose);
    animationQueues[uuid] = new carta::AnimationQueue(sessions[uuid]);
    msgQueues[uuid] = new tbb::concurrent_queue<tuple<string,uint32_t,vector<char>>>;
    fileSettings[uuid] = new carta::FileSettings(sessions[uuid]);

    log(uuid, "Client {} [{}] Connected. Clients: {}", uuid, ws->getAddress().address, sessions.size());
}

// Called on disconnect. Cleans up sessions. In future, we may want to delay this (in case of unintentional disconnects)
void onDisconnect(uWS::WebSocket<uWS::SERVER>* ws, int code, char* message, size_t length) {
    auto &uuid = *((string*)ws->getUserData());
    auto session = sessions[uuid];
    if (session) {
        delete session;
        sessions.erase(uuid);
        delete animationQueues[uuid];
        animationQueues.erase(uuid);
        delete msgQueues[uuid];
        msgQueues.erase(uuid);
        delete fileSettings[uuid];
        fileSettings.erase(uuid);
    }
    log(uuid, "Client {} [{}] Disconnected. Remaining clients: {}", uuid, ws->getAddress().address, sessions.size());
    delete &uuid;
}

// Forward message requests to session callbacks after parsing message into relevant ProtoBuf message
void onMessage(uWS::WebSocket<uWS::SERVER>* ws, char* rawMessage, size_t length, uWS::OpCode opCode) {
    auto uuid = *((string*)ws->getUserData());
    auto session = sessions[uuid];

    if (!session) {
        fmt::print("Missing session!\n");
        return;
    }

    if (opCode == uWS::OpCode::BINARY) {
        if (length > 36) {
            static const size_t max_len = 32;
            string eventName(rawMessage, min(strlen(rawMessage), max_len));
            uint32_t requestId = *reinterpret_cast<uint32_t*>(rawMessage+32);
            vector<char> eventPayload(&rawMessage[36], &rawMessage[length]);
            msgQueues[uuid]->push(make_tuple(eventName, requestId, eventPayload));
            OnMessageTask *omt = new(tbb::task::allocate_root()) OnMessageTask(
                uuid, session, msgQueues[uuid], animationQueues[uuid], fileSettings[uuid]);
            if(eventName == "SET_IMAGE_CHANNELS") {
                // has its own queue to keep channels in order during animation
                CARTA::SetImageChannels message;
                message.ParseFromArray(eventPayload.data(), eventPayload.size());
                animationQueues[uuid]->addRequest(message, requestId);
            } else if(eventName == "SET_IMAGE_VIEW") {
                CARTA::SetImageView message;
                message.ParseFromArray(eventPayload.data(), eventPayload.size());
                fileSettings[uuid]->addViewSetting(message, requestId);
            } else if(eventName == "SET_CURSOR") {
                CARTA::SetCursor message;
                message.ParseFromArray(eventPayload.data(), eventPayload.size());
                fileSettings[uuid]->addCursorSetting(message, requestId);
            }
            tbb::task::enqueue(*omt);
        }
    }
};

void exit_backend(int s) {
    // destroy objects cleanly
    cout << "Exiting backend." << endl;
    for (auto& session : sessions) {
        auto uuid = session.first;
        delete session.second;
        delete animationQueues[uuid];
        delete msgQueues[uuid];
        delete fileSettings[uuid];
    }
    sessions.clear();
    animationQueues.clear();
    msgQueues.clear();
    fileSettings.clear();

    exit(0);
}

// Entry point. Parses command line arguments and starts server listening
int main(int argc, const char* argv[]) {
    try {
        // set up interrupt signal handler
        struct sigaction sigHandler;
        sigHandler.sa_handler = exit_backend;
        sigemptyset(&sigHandler.sa_mask);
        sigHandler.sa_flags = 0;
        sigaction(SIGINT, &sigHandler, nullptr);

        // define and get input arguments
        int port(3002);
        int threadCount(tbb::task_scheduler_init::default_num_threads());
	{ // get values then let Input go out of scope
        casacore::Input inp;
        inp.version(version_id);
        inp.create("verbose", "False", "display verbose logging", "Bool");
        inp.create("permissions", "False", "use a permissions file for determining access", "Bool");
        inp.create("port", to_string(port), "set server port", "Int");
        inp.create("threads", to_string(threadCount), "set thread pool count", "Int");
        inp.create("base", baseFolder, "set folder for data files", "String");
        inp.create("root", rootFolder, "set top-level folder for data files", "String");
        inp.readArguments(argc, argv);

        verbose = inp.getBool("verbose");
        usePermissions = inp.getBool("permissions");
        port = inp.getInt("port");
        threadCount = inp.getInt("threads");
        baseFolder = inp.getString("base");
        rootFolder = inp.getString("root");
	}

	if (!checkRootBaseFolders(rootFolder, baseFolder)) {
	    return 1;
	}

        // Construct task scheduler, permissions
        tbb::task_scheduler_init task_sched(threadCount);
        if (usePermissions) {
            readPermissions("permissions.txt");
        }

        sessionNumber = 0;

        wsHub.onMessage(&onMessage);
        wsHub.onConnection(&onConnect);
        wsHub.onDisconnection(&onDisconnect);
        if (wsHub.listen(port)) {
            wsHub.getDefaultGroup<uWS::SERVER>().startAutoPing(5000);
            fmt::print("Listening on port {} with root folder {}, base folder {}, and {} threads in thread pool\n", port, rootFolder, baseFolder, threadCount);
            wsHub.run();
        } else {
            fmt::print("Error listening on port {}\n", port);
            return 1;
        }
    }
    catch (exception& e) {
        fmt::print("Error: {}\n", e.what());
        return 1;
    }
    catch (...) {
        fmt::print("Unknown error\n");
        return 1;
    }
    return 0;
}
