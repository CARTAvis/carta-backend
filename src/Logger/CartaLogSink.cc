/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "CartaLogSink.h"
#include "Logger.h"

#include <casacore/casa/Logging/LogFilter.h>
#include <casacore/casa/Logging/LogSinkInterface.h>

namespace carta {

CartaLogSink::CartaLogSink(casacore::LogMessage::Priority filter) : casacore::LogSinkInterface(casacore::LogFilter(filter)) {}

bool CartaLogSink::postLocally(const casacore::LogMessage& message) {
    if (filter().pass(message)) {
        auto priority = message.priority();
        auto log_message = "[casacore] " + message.message();
        if (message.priority() <= casacore::LogMessage::DEBUG1) {
            spdlog::debug(log_message);
        } else if (priority <= casacore::LogMessage::NORMAL) {
            spdlog::info(log_message);
        } else if (priority == casacore::LogMessage::WARN) {
            spdlog::warn(log_message);
        } else {
            spdlog::error(log_message);
        }
        return true;
    }

    return false;
}

casacore::String CartaLogSink::localId() {
    return casacore::String("CartaLogSink");
}

casacore::String CartaLogSink::id() const {
    return casacore::String("CartaLogSink");
}

} // namespace carta
