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
};

#endif // CARTA_BACKEND__MESSAGEHANDLER_H_