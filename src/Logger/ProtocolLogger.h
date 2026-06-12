/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_LOGGER_PROTOCOLLOGGER_H_
#define CARTA_SRC_LOGGER_PROTOCOLLOGGER_H_

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

#include <carta-protobuf/enums.pb.h>

namespace carta {

struct LoggingRule {
    std::string match;
    std::string action; // "normal", "throttle", "none"
    int32_t first_n = -1;
    int32_t every_m = -1;
};

class ProtocolLogger {
public:
    static constexpr const char* RECEIVE = "<-";
    static constexpr const char* SEND = "->";

    virtual ~ProtocolLogger() = default;

    static void Init();

    static ProtocolLogger& Instance() {
        return *instance;
    }

    static std::vector<LoggingRule> GetDefaultRules();

    virtual void LogEvent(CARTA::EventType type, const std::string& direction) = 0;

protected:
    static std::unique_ptr<ProtocolLogger> instance;
};

} // namespace carta

#endif // CARTA_SRC_LOGGER_PROTOCOLLOGGER_H_
