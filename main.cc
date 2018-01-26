#include <vector>
#include <fmt/format.h>
#include <uWS/uWS.h>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include "rapidjson/document.h"
#include "rapidjson/pointer.h"
#include "Session.h"

using namespace std;
using namespace rapidjson;

map<uWS::WebSocket<uWS::SERVER> *, Session *> sessions;
boost::uuids::random_generator uuid_gen;

string baseFolder = getenv("HOME");

void onConnect(uWS::WebSocket<uWS::SERVER> *ws, uWS::HttpRequest httpRequest) {
  sessions[ws] = new Session(ws, uuid_gen(), baseFolder);
  fmt::print("Client {} Connected. Clients: {}\n", boost::uuids::to_string(sessions[ws]->uuid), sessions.size());
}

void onDisconnect(uWS::WebSocket<uWS::SERVER> *ws, int code, char *message, size_t length) {
  auto uuid = sessions[ws]->uuid;
  auto session = sessions[ws];
  if (session) {
    delete session;
    sessions.erase(ws);
  }

  fmt::print("Client {} Disconnected. Remaining clients: {}\n", boost::uuids::to_string(uuid), sessions.size());
}

// Forward message requests to session callbacks
void onMessage(uWS::WebSocket<uWS::SERVER> *ws, char *rawMessage, size_t length, uWS::OpCode opCode) {
  auto session = sessions[ws];

  if (!session) {
    fmt::print("Missing session!\n");
    return;
  }

  if (opCode==uWS::OpCode::TEXT) {
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

      if (eventName=="region_read") {
        session->onRegionRead(message);
      } else if (eventName=="fileload") {
        session->onFileLoad(message);
      } else {
        fmt::print("Unknown query type!\n");
      }

    } else
      fmt::print("Missing event or message parameters\n");
  } else if (opCode==uWS::OpCode::BINARY)
    fmt::print("Binary recieved ({} bytes)\n", length);
};

int main() {
  uWS::Hub h;

  h.onMessage(&onMessage);
  h.onConnection(&onConnect);
  h.onDisconnection(&onDisconnect);
  if (h.listen(3002)) {
    h.run();
  }

}