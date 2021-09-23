/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# CartaGrpcService.cc: grpc server to receive messages from the python scripting client

#include "CartaGrpcService.h"

#include <chrono>

#include <spdlog/fmt/fmt.h>

#include <carta-protobuf/defs.pb.h>
#include <carta-protobuf/enums.pb.h>
#include <carta-protobuf/scripting.pb.h>

uint32_t CartaGrpcService::_scripting_request_id = 0;

void GrpcSilentLogger(gpr_log_func_args*) {}

extern void gpr_default_log(gpr_log_func_args* args);

CartaGrpcService::CartaGrpcService(std::string auth_token) : _auth_token(auth_token) {}

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
    auto return_path = request->return_path();

    auto metadata = context->client_metadata();

    std::string token;

    if (!_auth_token.empty()) {
        for (auto& m : metadata) {
            if (m.first == "token") {
                token = std::string(m.second.begin(), m.second.size());
                break;
            }
        }
    }

    grpc::Status status(grpc::Status::OK);

    if (!_auth_token.empty() && token != _auth_token) {
        status = grpc::Status(grpc::StatusCode::PERMISSION_DENIED, "Invalid token.");
    } else if (_sessions.find(session_id) == _sessions.end()) {
        status = grpc::Status(grpc::StatusCode::OUT_OF_RANGE, fmt::format("Invalid session ID {}.", session_id));
    } else {
        _scripting_request_id++;
        _scripting_request_id = std::max(_scripting_request_id, 1u);

        auto session = _sessions[session_id].first;
        session->SendScriptingRequest(_scripting_request_id, path, action, parameters, async, return_path);

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

GrpcManager::GrpcManager() : _selected_port(-1) {}

GrpcManager::GrpcManager(int port, std::string auth_token) : _selected_port(-1) {
    // Silence grpc error log
    gpr_set_log_function(GrpcSilentLogger);

    // Set up address buffer
    std::string server_address = fmt::format("0.0.0.0:{}", port);

    // Build grpc service
    grpc::ServerBuilder builder;
    // Listen on the given address without any authentication mechanism.
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials(), &_selected_port);
    // Register and start carta grpc server
    _service = std::make_shared<CartaGrpcService>(auth_token);
    builder.RegisterService(_service.get());
    // By default ports can be reused; we don't want this
    builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
    _server = builder.BuildAndStart();

    // Turn logging back on
    gpr_set_log_function(gpr_default_log);
}

GrpcManager::~GrpcManager() {
    if (_server) {
        _server->Shutdown();
    }
}

bool GrpcManager::Listening() {
    return (_selected_port > 0);
}

std::shared_ptr<CartaGrpcService> GrpcManager::Service() {
    return _service;
}
