#include <vector>
#include <fmt/format.h>
#include <uWS/uWS.h>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/program_options.hpp>
#include "rapidjson/document.h"
#include "rapidjson/pointer.h"
#include "Session.h"

using namespace std;
using namespace rapidjson;
using namespace uWS;
namespace po = boost::program_options;

map<WebSocket<SERVER> *, Session *> sessions;
boost::uuids::random_generator uuid_gen;

string baseFolder = "./";
bool verbose = false;

void onConnect(WebSocket<SERVER> *ws, HttpRequest httpRequest) {
  sessions[ws] = new Session(ws, uuid_gen(), baseFolder, verbose);
  fmt::print("Client {} Connected. Clients: {}\n", boost::uuids::to_string(sessions[ws]->uuid), sessions.size());
}

void onDisconnect(WebSocket<SERVER> *ws, int code, char *message, size_t length) {
  auto uuid = sessions[ws]->uuid;
  auto session = sessions[ws];
  if (session) {
    delete session;
    sessions.erase(ws);
  }

  fmt::print("Client {} Disconnected. Remaining clients: {}\n", boost::uuids::to_string(uuid), sessions.size());
}

// Forward message requests to session callbacks
void onMessage(WebSocket<SERVER> *ws, char *rawMessage, size_t length, OpCode opCode) {
  auto session = sessions[ws];

  if (!session) {
    fmt::print("Missing session!\n");
    return;
  }

  if (opCode == OpCode::TEXT) {
    // Null-terminate string to prevent parsing errors
    char *paddedMessage = new char[length + 1];
    memcpy(paddedMessage, rawMessage, length);
    paddedMessage[length] = 0;

    Document d;
    d.Parse(paddedMessage);
    delete[] paddedMessage;

    if (d.HasMember("event") && d.HasMember("message") && d["message"].IsObject()) {
      string eventName(d["event"].GetString());
      Value &message = GetValueByPointerWithDefault(d, "/message", "{}");

      if (eventName == "region_read") {
        session->onRegionRead(message);
      } else if (eventName == "fileload") {
        session->onFileLoad(message);
      } else {
        fmt::print("Unknown query type!\n");
      }

    } else
      fmt::print("Missing event or message parameters\n");
  } else if (opCode == OpCode::BINARY)
    fmt::print("Binary recieved ({} bytes)\n", length);
};

int main(int argc, const char *argv[]) {
  try {
    po::options_description desc("Allowed options");
    desc.add_options()
        ("help", "produce help message")
        ("verbose", "display verbose logging")
        ("port", po::value<int>(), "set server port")
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
    if (vm.count("folder")) {
      baseFolder = vm["folder"].as<string>();
    }

    Hub h;

    h.onMessage(&onMessage);
    h.onConnection(&onConnect);
    h.onDisconnection(&onDisconnect);
    if (h.listen(port)) {
      fmt::print("Listening on port {} with data folder {}\n", port, baseFolder);
      h.run();
    } else {
      fmt::print("Error listening on port {}\n", port);
      return 1;
    }
  }
  catch (exception &e) {
    fmt::print("Error: {}\n", e.what());
    return 1;
  }
  catch (...) {
    fmt::print("Unknown error\n");
    return 1;
  }
  return 0;
}