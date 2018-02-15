#include <fmt/format.h>
#include <google/protobuf/message_lite.h>
#include "events.h"
#include "rapidjson/writer.h"

using namespace uWS;
using namespace rapidjson;
using namespace std;

char* rawData = nullptr;
size_t rawDataSize = 0;

// Sends an event to the client with a given JSON document
void sendEvent(WebSocket<SERVER> *ws, Document &document) {
  StringBuffer buffer;
  Writer<StringBuffer> writer(buffer);
  document.Accept(writer);
  string jsonPayload = buffer.GetString();
  ws->send(jsonPayload.c_str(), jsonPayload.size(), uWS::TEXT);
}

// Sends an event to the client with a given JSON document, prepended by a binary payload of the specified length
void sendEventBinaryPayload(WebSocket<SERVER> *ws, Document &document, void *payload, uint32_t length) {
  StringBuffer buffer;
  Writer<StringBuffer> writer(buffer);
  document.Accept(writer);
  string jsonPayload = buffer.GetString();
  size_t requiredSize = jsonPayload.size() + length + sizeof(length);
  if (!rawData || rawDataSize < requiredSize){
    delete [] rawData;
    rawData = new char[requiredSize];
    rawDataSize = requiredSize;
  }
  memcpy(rawData, &length, sizeof(length));
  memcpy(rawData + sizeof(length), payload, length);
  memcpy(rawData + length + sizeof(length), jsonPayload.c_str(), jsonPayload.size());
  ws->send(rawData, requiredSize, uWS::BINARY);
}

// Sends an event to the client with a given event name (padded/concatenated to 32 characters) and a given protobuf message
void sendEvent(uWS::WebSocket<uWS::SERVER> *ws, string eventName, google::protobuf::MessageLite& message) {
  size_t eventNameLength = 32;
  int messageLength = message.ByteSize();
  size_t requiredSize = eventNameLength + messageLength;
  if (!rawData || rawDataSize < requiredSize){
    delete [] rawData;
    rawData = new char[requiredSize];
    rawDataSize = requiredSize;
  }
  memset(rawData, 0, eventNameLength);
  memcpy(rawData, eventName.c_str(), min(eventName.length(), eventNameLength));
  message.SerializeToArray(rawData+eventNameLength, messageLength);
  ws->send(rawData, requiredSize, uWS::BINARY);
}

// Sends an event to the client with a given event name (padded/concatenated to 32 characters)
// and a given protobuf message, prepended by a binary payload of the specified length
void sendEventBinaryPayload(uWS::WebSocket<uWS::SERVER> *ws, std::string eventName, void* payload, uint32_t payloadLength, google::protobuf::MessageLite& message) {
  size_t eventNameLength = 32;
  int messageLength = message.ByteSize();
  size_t requiredSize = eventNameLength + sizeof(payloadLength) + payloadLength + messageLength;
  if (!rawData || rawDataSize < requiredSize){
    delete [] rawData;
    rawData = new char[requiredSize];
    rawDataSize = requiredSize;
  }
  memset(rawData, 0, eventNameLength);
  memcpy(rawData, eventName.c_str(), min(eventName.length(), eventNameLength));
  memcpy(rawData+eventNameLength, &payloadLength, sizeof(payloadLength));
  if (payloadLength) {
    memcpy(rawData+eventNameLength+ sizeof(payloadLength), payload, payloadLength);
  }
  message.SerializeToArray(rawData+eventNameLength+ sizeof(payloadLength) + payloadLength, messageLength);
  ws->send(rawData, requiredSize, uWS::BINARY);
}
