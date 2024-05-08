/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_SESSION_CURSORSETTINGS_H_
#define CARTA_SRC_SESSION_CURSORSETTINGS_H_

#include <mutex>
#include <unordered_map>
#include <utility>

#include "ThreadingManager/Concurrency.h"

#include <carta-protobuf/set_cursor.pb.h>

namespace carta {

class Session;

class CursorSettings {
public:
    CursorSettings(Session* session);

    void AddCursorSetting(const CARTA::SetCursor& message, uint32_t request_id);
    // TODO: change to event type
    bool ExecuteOne(const std::string& event_name, const uint32_t file_id);
    void ClearSettings(const uint32_t file_id);

private:
    Session* _session;
    queuing_rw_mutex _cursor_mutex;

    // pair is <message, requestId)
    using cursor_info_t = std::pair<CARTA::SetCursor, uint32_t>;
    using cursor_iter = std::unordered_map<uint32_t, cursor_info_t>::iterator;
    // map is <fileId, cursor info>
    std::unordered_map<uint32_t, cursor_info_t> _latest_cursor;
};

} // namespace carta

#endif // CARTA_SRC_SESSION_CURSORSETTINGS_H_
