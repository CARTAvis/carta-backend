#ifndef CARTA_BACKEND__RESUMESESSIONHANDLER_H_
#define CARTA_BACKEND__RESUMESESSIONHANDLER_H_

#include <fmt/format.h>

#include "EventHeader.h"
#include "OnMessageTask.h"
#include "Session.h"
#include "Util.h"

class ResumeSessionHandler {
public:
    explicit ResumeSessionHandler(Session* session, CARTA::ResumeSession message, uint32_t request_id);

private:
    Session* _session;
    uint32_t _request_id;
    CARTA::ResumeSession _message;

    void Execute();

    void CloseFileCmd(CARTA::CloseFile message);
    bool OpenFileCmd(CARTA::OpenFile message);
    void SetImageChannelsCmd(CARTA::SetImageChannels message);
    bool SetRegionCmd(CARTA::SetRegion message);
};

#endif // CARTA_BACKEND__RESUMESESSIONHANDLER_H_
