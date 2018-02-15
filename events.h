#pragma once
#include <uWS/uWS.h>
#include "rapidjson/document.h"

void sendEvent(uWS::WebSocket<uWS::SERVER> *ws, rapidjson::Document &document);
void sendEventBinaryPayload(uWS::WebSocket<uWS::SERVER> *ws, rapidjson::Document &document, void *payload, uint32_t length);

// protocol buffer versions
void sendEvent(uWS::WebSocket<uWS::SERVER> *ws, std::string eventName, google::protobuf::MessageLite& message);
void sendEventBinaryPayload(uWS::WebSocket<uWS::SERVER> *ws, std::string eventName, void* payload, uint32_t payloadLength, google::protobuf::MessageLite& message);

