/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# CartaGrpcService.cc: grpc server to receive messages from the python scripting client

#include "CartaGrpcService.h"

#include <chrono>

#include <carta-protobuf/defs.pb.h>
#include <carta-protobuf/enums.pb.h>
#include <carta-protobuf/scripting.pb.h>

#include "../InterfaceConstants.h"
#include "../Util.h"

uint32_t CartaGrpcService::_scripting_request_id = 0;

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

grpc::Status CartaGrpcService::CallAction(
    grpc::ServerContext* context, const CARTA::script::ActionRequest* request, CARTA::script::ActionReply* reply) {
    auto session_id = request->session_id();
    // TODO: rename path to target in the protobuf file
    auto path = request->path();
    auto action = request->action();
    auto parameters = request->parameters();
    auto async = request->async();

    grpc::Status status(grpc::Status::OK);

    if (_sessions.find(session_id) == _sessions.end()) {
        status = grpc::Status(grpc::StatusCode::OUT_OF_RANGE, fmt::format("Invalid session ID {}.", session_id));
    } else {
        _scripting_request_id++;
        _scripting_request_id = std::max(_scripting_request_id, 1u);

        auto session = _sessions[session_id].first;
        session->SendScriptingRequest(_scripting_request_id, path, action, parameters, async);

        auto t_start = std::chrono::system_clock::now();
        while (!session->GetScriptingResponse(_scripting_request_id, reply)) {
            auto t_end = std::chrono::system_clock::now();
            std::chrono::duration<double> elapsed_sec = t_end - t_start;
            if (elapsed_sec.count() > SCRIPTING_TIMEOUT) {
                // TODO: more specific error
                return grpc::Status(
                    grpc::StatusCode::DEADLINE_EXCEEDED, fmt::format("Scripting request to session {} timed out.", session_id));
            }
        }
    }

    return status;
}
