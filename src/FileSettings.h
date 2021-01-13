/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# FileSettings.h: uses tbb::concurrent_unordered_map to keep latest per-file settings
//# Used for cursor and view settings

#ifndef CARTA_BACKEND__FILESETTINGS_H_
#define CARTA_BACKEND__FILESETTINGS_H_

#include <mutex>
#include <utility>

#include <tbb/concurrent_unordered_map.h>
#include <tbb/queuing_rw_mutex.h>

#include <carta-protobuf/set_cursor.pb.h>

class Session;

class FileSettings {
public:
    FileSettings(Session* session);

    void AddCursorSetting(const CARTA::SetCursor& message, uint32_t request_id);
    // TODO: change to event type
    bool ExecuteOne(const std::string& event_name, const uint32_t file_id);
    void ClearSettings(const uint32_t file_id);

private:
    Session* _session;
    tbb::queuing_rw_mutex _cursor_mutex;

    // pair is <message, requestId)
    using cursor_info_t = std::pair<CARTA::SetCursor, uint32_t>;
    using cursor_iter = tbb::concurrent_unordered_map<uint32_t, cursor_info_t>::iterator;
    // map is <fileId, cursor info>
    tbb::concurrent_unordered_map<uint32_t, cursor_info_t> _latest_cursor;
};

#endif // CARTA_BACKEND__FILESETTINGS_H_
