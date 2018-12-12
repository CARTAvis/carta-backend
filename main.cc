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

#define MAX_THREADS 4

using namespace std;
using namespace uWS;

// key is uuid:
using key_type = std::string;
unordered_map<key_type, Session*> sessions;
unordered_map<key_type, carta::AnimationQueue*> animationQueues;
unordered_map<key_type, carta::FileSettings*> fileSettings;
// msgQueue holds eventName, requestId, message
unordered_map<key_type, tbb::concurrent_queue<tuple<string,uint32_t,vector<char>>>*> msgQueues;

// key is current folder
unordered_map<string, vector<string>> permissionsMap;

int sessionNumber;
Hub h;

std::string baseFolder("./"), version_id("1.0");
bool verbose, usePermissions;

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
void onConnect(WebSocket<SERVER>* ws, HttpRequest httpRequest) {
    std::string uuidstr = fmt::format("{}{}", ++sessionNumber,
        casacore::Int(casacore::HostInfo::secondsFrom1970()));
    ws->setUserData(new std::string(uuidstr));
    auto &uuid = *((std::string*)ws->getUserData());

    uS::Async *outgoing = new uS::Async(h.getLoop());
    outgoing->setData(&uuid);
    outgoing->start(
        [](uS::Async *async) -> void {
            auto uuid = *((std::string*)async->getData());
            sessions[uuid]->sendPendingMessages();
        });

    sessions[uuid] = new Session(ws, uuid, permissionsMap, usePermissions, baseFolder, outgoing, verbose);
    animationQueues[uuid] = new carta::AnimationQueue(sessions[uuid]);
    msgQueues[uuid] = new tbb::concurrent_queue<tuple<string,uint32_t,vector<char>>>;
    fileSettings[uuid] = new carta::FileSettings(sessions[uuid]);

    log(uuid, "Client {} [{}] Connected. Clients: {}", uuid, ws->getAddress().address, sessions.size());
}

// Called on disconnect. Cleans up sessions. In future, we may want to delay this (in case of unintentional disconnects)
void onDisconnect(WebSocket<SERVER>* ws, int code, char* message, size_t length) {
    auto &uuid = *((std::string*)ws->getUserData());
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
void onMessage(WebSocket<SERVER>* ws, char* rawMessage, size_t length, OpCode opCode) {
    auto uuid = *((std::string*)ws->getUserData());
    auto session = sessions[uuid];

    if (!session) {
        fmt::print("Missing session!\n");
        return;
    }

    if (opCode == OpCode::BINARY) {
        if (length > 36) {
            static const size_t max_len = 32;
            std::string eventName(rawMessage, std::min(std::strlen(rawMessage), max_len));
            uint32_t requestId = *reinterpret_cast<uint32_t*>(rawMessage+32);
            std::vector<char> eventPayload(&rawMessage[36], &rawMessage[length]);
            msgQueues[uuid]->push(std::make_tuple(eventName, requestId, eventPayload));
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
    } else {
        log(uuid, "Invalid event type");
    }
};

// Entry point. Parses command line arguments and starts server listening
int main(int argc, const char* argv[]) {
    try {
        // define and get input arguments
        casacore::Input inp;
        inp.version(version_id);
        inp.create("verbose", "False", "display verbose logging", "Bool");
        inp.create("permissions", "False", "use a permissions file for determining access", "Bool");
        int port(3002);
        inp.create("port", std::to_string(port), "set server port", "Int");
        int threadCount(tbb::task_scheduler_init::default_num_threads());
        inp.create("threads", std::to_string(threadCount), "set thread pool count", "Int");
        inp.create("folder", baseFolder, "set folder for data files", "String");
        inp.readArguments(argc, argv);

        verbose = inp.getBool("verbose");
        usePermissions = inp.getBool("permissions");
        port = inp.getInt("port");
        threadCount = inp.getInt("threads");
        baseFolder = inp.getString("folder");

        // Construct task scheduler, permissions
        tbb::task_scheduler_init task_sched(threadCount);
        if (usePermissions) {
            readPermissions("permissions.txt");
        }

        sessionNumber = 0;

        h.onMessage(&onMessage);
        h.onConnection(&onConnect);
        h.onDisconnection(&onDisconnect);
        if (h.listen(port)) {
            h.getDefaultGroup<uWS::SERVER>().startAutoPing(5000);
            fmt::print("Listening on port {} with data folder {} and {} threads in thread pool\n", port, baseFolder, threadCount);
            h.run();
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
