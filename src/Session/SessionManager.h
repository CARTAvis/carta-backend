/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_SESSION_SESSIONMANAGER_H_
#define CARTA_SRC_SESSION_SESSIONMANAGER_H_

#include <uWebSockets/App.h>
#include <functional>
#include <unordered_map>
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

    typedef void (SessionManager::*MessageHandler)(Session*, std::string_view, const EventHeader&);

private:
    // message handlers
    static std::unordered_map<CARTA::EventType, MessageHandler> _message_handlers;

    void RegisterViewerHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void ResumeSessionHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void SetImageChannelsHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void SetCursorHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void SetHistogramRequirementsHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void CloseFileHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void StartAnimationHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void AnimationFlowControlHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void FileInfoRequestHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void OpenFileHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void AddRequiredTilesHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void RegionFileInfoRequestHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void ImportRegionHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void ExportRegionHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void SetContourParametersHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void ScriptingResponseHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void SetRegionHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void RemoveRegionHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void SetSpectralRequirementsHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void CatalogFileInfoRequestHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void OpenCatalogFileHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void CloseCatalogFileHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void CatalogFilterRequestHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void StopMomentCalcHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void SaveFileHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void ConcatStokesFilesHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void StopFileListHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void SetSpatialRequirementsHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void SetStatsRequirementsHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void MomentRequestHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void FileListRequestHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void RegionListRequestHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void CatalogListRequestHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void PvRequestHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void StopPvCalcHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void FittingRequestHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void SetVectorOverlayParametersHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void StopFittingHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void StopPvPreviewHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void ClosePvReviewHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void RemoteFileRequestHandler(Session* session, std::string_view sv_message, const EventHeader& head);
    void ChannelMapFlowControlHandler(Session* session, std::string_view sv_message, const EventHeader& head);

    // Sessions map
    uint32_t _session_number;
    std::unordered_map<uint32_t, Session*> _sessions;
    std::mutex _sessions_mutex;
    // Map from internal session ID to actual session ID
    std::unordered_map<uint32_t, uint32_t> _real_session_id;
    // uWebSockets app
    uWS::App _app;
    // Shared objects
    ProgramSettings& _settings;
    std::string _auth_token;
    std::shared_ptr<FileListHandler> _file_list_handler;

    std::string IPAsText(std::string_view binary);
};
} // namespace carta
#endif // CARTA_SRC_SESSION_SESSIONMANAGER_H_
