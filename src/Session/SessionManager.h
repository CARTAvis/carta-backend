/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_SRC_SESSIONMANAGER_SESSIONMANAGER_H_
#define CARTA_BACKEND_SRC_SESSIONMANAGER_SESSIONMANAGER_H_

#include <uWebSockets/App.h>
#include <vector>

#include "Main/ProgramSettings.h"
#include "Session.h"

#define MAX_SOCKET_PORT_TRIALS 100

namespace carta {
class SessionManager {
public:
    using WSType = uWS::WebSocket<false, true, PerSocketData>;
    SessionManager(ProgramSettings& settings, std::string auth_token, std::shared_ptr<FileListHandler>);
    void DeleteSession(uint32_t session_id);
    void OnUpgrade(uWS::HttpResponse<false>* http_response, uWS::HttpRequest* http_request, struct us_socket_context_t* context);
    // Called on connection. Creates session objects and assigns UUID to it
    void OnConnect(WSType* ws);
    // Called on disconnect. Cleans up sessions. In future, we may want to delay this (in case of unintentional disconnects)
    void OnDisconnect(WSType* ws, int code, std::string_view message);
    void OnDrain(WSType* ws);
    // Forward message requests to session callbacks after parsing message into relevant ProtoBuf message
    void OnMessage(WSType* ws, std::string_view sv_message, uWS::OpCode op_code);
    void Listen(std::string host, std::vector<int> ports, int default_port, int& port);
    uWS::App& App();
    void RunApp();
    bool SendScriptingRequest(int& session_id, uint32_t& scripting_request_id, std::string& target, std::string& action,
        std::string& parameters, bool& async, std::string& return_path, ScriptingResponseCallback callback,
        ScriptingSessionClosedCallback session_closed_callback);
    void OnScriptingAbort(int session_id, uint32_t scripting_request_id);

private:
    // Sessions map
    uint32_t _session_number;
    std::unordered_map<uint32_t, Session*> _sessions;
    std::mutex _sessions_mutex;
    // uWebSockets app
    uWS::App _app;
    // Shared objects
    ProgramSettings& _settings;
    std::string _auth_token;
    std::shared_ptr<FileListHandler> _file_list_handler;

    std::string IPAsText(std::string_view binary);
};
} // namespace carta
#endif // CARTA_BACKEND_SRC_SESSIONMANAGER_SESSIONMANAGER_H_
