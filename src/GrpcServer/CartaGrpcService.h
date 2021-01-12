/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# CartaGrpcService.h: service for a grpc client

#ifndef CARTA_BACKEND_GRPCSERVER_CARTAGRPCSERVICE_H_
#define CARTA_BACKEND_GRPCSERVER_CARTAGRPCSERVICE_H_

// #include <condition_variable>

#include <grpc++/grpc++.h>

#include <carta-scripting-grpc/carta_service.grpc.pb.h>

#include "../Session.h"

class CartaGrpcService : public CARTA::script::CartaBackend::Service {
public:
    CartaGrpcService(bool verbose);
    void AddSession(Session* session);
    void RemoveSession(Session* session);

    grpc::Status CallAction(grpc::ServerContext* context, const CARTA::script::ActionRequest* request, CARTA::script::ActionReply* reply);

private:
    // Map session_id to <Session*, connected>
    std::unordered_map<int, std::pair<Session*, bool>> _sessions;
    bool _verbose;

    static uint32_t _scripting_request_id;
};

#endif // CARTA_BACKEND_GRPCSERVER_CARTAGRPCSERVICE_H_
