#include <vector>
#include <fmt/format.h>
#include <uWS/uWS.h>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/program_options.hpp>
#include <proto/fileLoadRequest.pb.h>
#include <proto/regionReadRequest.pb.h>
#include <proto/profileRequest.pb.h>
#include "ctpl.h"
#include "Session.h"

#define MAX_THREADS 4

using namespace std;
using namespace uWS;
namespace po = boost::program_options;

map<WebSocket<SERVER>*, Session*> sessions;
boost::uuids::random_generator uuid_gen;

string baseFolder = "./";
bool verbose = false;
ctpl::thread_pool threadPool;

string getEventName(char* rawMessage) {
    int nullIndex = 0;
    for (auto i = 0; i < 32; i++) {
        if (!rawMessage[i]) {
            nullIndex = i;
            break;
        }
    }
    return string(rawMessage, nullIndex);
}

void onConnect(WebSocket<SERVER>* ws, HttpRequest httpRequest) {
    sessions[ws] = new Session(ws, uuid_gen(), baseFolder, threadPool, verbose);
    time_t time = chrono::system_clock::to_time_t(chrono::system_clock::now());
    string timeString = ctime(&time);
    timeString = timeString.substr(0, timeString.length() - 1);

    fmt::print("Client {} [{}] Connected ({}). Clients: {}\n", boost::uuids::to_string(sessions[ws]->uuid), ws->getAddress().address, timeString, sessions.size());
}

void onDisconnect(WebSocket<SERVER>* ws, int code, char* message, size_t length) {
    auto uuid = sessions[ws]->uuid;
    auto session = sessions[ws];
    if (session) {
        delete session;
        sessions.erase(ws);
    }
    time_t time = chrono::system_clock::to_time_t(chrono::system_clock::now());
    string timeString = ctime(&time);
    timeString = timeString.substr(0, timeString.length() - 1);
    fmt::print("Client {} [{}] Disconnected ({}). Remaining clients: {}\n", boost::uuids::to_string(uuid), ws->getAddress().address, timeString, sessions.size());
}

// Forward message requests to session callbacks after parsing message
void onMessage(WebSocket<SERVER>* ws, char* rawMessage, size_t length, OpCode opCode) {
    auto session = sessions[ws];

    if (!session) {
        fmt::print("Missing session!\n");
        return;
    }

    if (opCode == OpCode::BINARY) {
        if (length > 32) {
            string eventName = getEventName(rawMessage);
            if (eventName == "fileload") {
                Requests::FileLoadRequest fileLoadRequest;
                if (fileLoadRequest.ParseFromArray(rawMessage + 32, length - 32)) {
                    session->onFileLoad(fileLoadRequest);
                }
            } else if (eventName == "region_read") {
                Requests::RegionReadRequest regionReadRequest;
                if (regionReadRequest.ParseFromArray(rawMessage + 32, length - 32)) {
                    session->onRegionReadRequest(regionReadRequest);
                }
            } else if (eventName == "profile") {
                Requests::ProfileRequest profileRequest;
                if (profileRequest.ParseFromArray(rawMessage + 32, length - 32)) {
                    session->onProfileRequest(profileRequest);
                }
            } else if (eventName == "region_stats") {
                Requests::RegionStatsRequest statsRequest;
                if (statsRequest.ParseFromArray(rawMessage + 32, length - 32)) {
                    session->onRegionStatsRequest(statsRequest);
                }
            } else {
                fmt::print("Unknown event type {}\n", eventName);
            }
        }
    } else {
        fmt::print("Invalid event type\n");
    }
};

int main(int argc, const char* argv[]) {
    try {
        po::options_description desc("Allowed options");
        desc.add_options()
            ("help", "produce help message")
            ("verbose", "display verbose logging")
            ("port", po::value<int>(), "set server port")
            ("threads", po::value<int>(), "set thread pool count")
            ("folder", po::value<string>(), "set folder for data files");

        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);

        if (vm.count("help")) {
            std::cout << desc << "\n";
            return 0;
        }

        verbose = vm.count("verbose");

        int port = 3002;
        if (vm.count("port")) {
            port = vm["port"].as<int>();
        }

        int threadCount = MAX_THREADS;
        if (vm.count("threads")) {
            threadCount = vm["threads"].as<int>();
        }
        threadPool.resize(threadCount);

        if (vm.count("folder")) {
            baseFolder = vm["folder"].as<string>();
        }

        Hub h;

        h.onMessage(&onMessage);
        h.onConnection(&onConnect);
        h.onDisconnection(&onDisconnect);
        if (h.listen(port)) {
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