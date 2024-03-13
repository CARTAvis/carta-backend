/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_SESSION_SESSIONCONTEXT_H_
#define CARTA_SRC_SESSION_SESSIONCONTEXT_H_

namespace carta {

class SessionContext {
public:
    SessionContext() {
        _cancelled = false;
    }
    void cancel_group_execution() {
        _cancelled = true;
    }
    bool is_group_execution_cancelled() {
        return _cancelled;
    }
    void reset() {
        _cancelled = false;
    }

private:
    volatile bool _cancelled;
};

} // namespace carta

#endif // CARTA_SRC_SESSION_SESSIONCONTEXT_H_
