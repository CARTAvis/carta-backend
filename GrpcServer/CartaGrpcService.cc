//# CartaGrpcService.cc: grpc server to receive messages from the python scripting client

#include "CartaGrpcService.h"

#include <chrono>

#include <carta-protobuf/defs.pb.h>
#include <carta-protobuf/enums.pb.h>

#include "../InterfaceConstants.h"
#include "../Util.h"

CartaGrpcService::CartaGrpcService(bool verbose) : _verbose(verbose) {}

void CartaGrpcService::AddSession(Session* session) {
    // Map session to its ID, set connected to false
    auto session_id = session->GetId();
    std::pair<Session*, bool> session_info(session, false);
    _sessions[session_id] = session_info;
}

void CartaGrpcService::RemoveSession(Session* session) {
    // Remove Session from map
    auto session_id = session->GetId();
    if (_sessions.count(session_id)) {
        _sessions.erase(session_id);
    }
}

grpc::Status CartaGrpcService::CallAction(grpc::ServerContext* context, const CARTAVIS::ActionRequest* request, CARTAVIS::ActionReply* reply) {
    auto session_id = request->session_id;
    auto path = request->path;
    auto action = request->action;
    auto parameters = request->parameters;
    auto async = request->async;
    
    grpc::Status status(grpc::Status::OK);
    
    if (_sessions.find(session_id) == _sessions.end()) {
        status = grpc::Status(grpc::StatusCode::OUT_OF_RANGE, fmt::format("Invalid session ID {}", session_id));
    } else {
        // TODO TODO TODO: call the frontend here via the session
        // TODO: Add to session:
        // function to call request event
        // handler for response event
        // map to store request UUIDs and responses
        // here: wait for response or time out
        // How to clean up timed out responses from map?
        // Add set of timed out uuids; prune response map when it's bigger than some max size
        reply->set_success(true);
        reply->set_message("foobar");
        reply->set_response("{\"foo\": \"bar\"}");
    }
    
    return status;
}
