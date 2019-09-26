#ifndef CARTA_BACKEND__MESSAGEHANDLER_H_
#define CARTA_BACKEND__MESSAGEHANDLER_H_

#include <fmt/format.h>

#include "EventHeader.h"
#include "OnMessageTask.h"
#include "Session.h"
#include "Util.h"

class MessageHandler {
public:
    MessageHandler(Session* session, char* raw_message, size_t length);

private:
    Session* _session;
    char* _event_buf;
    size_t _event_length;
    carta::EventHeader _header;

    void Execute();

    void Command(CARTA::RegisterViewer message);
    void Command(CARTA::SetImageChannels message);
    void Command(CARTA::SetImageView message);
    void Command(CARTA::SetCursor message);
    void Command(CARTA::SetHistogramRequirements message);
    void Command(CARTA::CloseFile message);
    void Command(CARTA::StartAnimation message);
    void Command(CARTA::StopAnimation message);
    void Command(CARTA::AnimationFlowControl message);
    void Command(CARTA::FileInfoRequest message);
    void Command(CARTA::FileListRequest message);
    void Command(CARTA::OpenFile message);
    void Command(CARTA::AddRequiredTiles message);
    void Command(CARTA::RegionListRequest message);
    void Command(CARTA::RegionFileInfoRequest message);
    void Command(CARTA::ImportRegion message);
    void Command(CARTA::ExportRegion message);
    void Command(CARTA::SetSpatialRequirements message);
    void Command(CARTA::SetSpectralRequirements message);
    void Command(CARTA::SetStatsRequirements message);
    void Command(CARTA::SetRegion message);
    void Command(CARTA::RemoveRegion message);
};

#endif // CARTA_BACKEND__MESSAGEHANDLER_H_