/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_LOGGER_CARTALOGSINK_H_
#define CARTA_SRC_LOGGER_CARTALOGSINK_H_

#include <casacore/casa/Logging/LogSinkInterface.h>

namespace carta {

class CartaLogSink : public casacore::LogSinkInterface {
public:
    CartaLogSink() = default;
    explicit CartaLogSink(casacore::LogMessage::Priority filter);
    ~CartaLogSink() = default;

    // Write message to the spdlog if it passes the filter.
    virtual bool postLocally(const casacore::LogMessage& message);

    // OVERRIDE? Write any pending output.
    // virtual void flush (bool global=True) override;

    // Returns the id for this class.
    static casacore::String localId();

    // Returns the id of the LogSink in use.
    virtual casacore::String id() const;
};

} // namespace carta

#endif // CARTA_SRC_LOGGER_CARTALOGSINK_H_
