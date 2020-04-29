//# CartaGrpcService.h: service for a grpc client

#ifndef CARTA_BACKEND_GRPCSERVER_CARTAGRPCSERVICE_H_
#define CARTA_BACKEND_GRPCSERVER_CARTAGRPCSERVICE_H_

// #include <condition_variable>

#include <grpc++/grpc++.h>

#include <cartavis/carta_service.grpc.pb.h>

#include "../Session.h"

class CartaGrpcService : public CARTAVIS::CartaBackend::Service {
public:
    CartaGrpcService(bool verbose);
    void AddSession(Session* session);
    void RemoveSession(Session* session);
    
    grpc::Status CallAction(grpc::ServerContext* context, const CARTAVIS::ActionRequest* request, CARTAVIS::ActionReply* reply);

private:
    // Map session_id to <Session*, connected>
    std::unordered_map<int, std::pair<Session*, bool>> _sessions;
};

#endif // CARTA_BACKEND_GRPCSERVER_CARTAGRPCSERVICE_H_
