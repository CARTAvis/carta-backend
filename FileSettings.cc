/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "FileSettings.h"
#include "Session.h"

FileSettings::FileSettings(Session* session) : _session(session) {}

void FileSettings::AddCursorSetting(const CARTA::SetCursor& message, uint32_t request_id) {
    FileSettings::cursor_info_t settings = std::make_pair(message, request_id);
    uint32_t file_id(message.file_id());
    bool write_lock(false); // concurrency okay
    tbb::queuing_rw_mutex::scoped_lock lock(_cursor_mutex, write_lock);
    FileSettings::cursor_iter cursor_results = _latest_cursor.find(file_id);
    if (cursor_results != _latest_cursor.end()) { // replace with new settings
        cursor_results->second = settings;
    } else {
        _latest_cursor.insert(cursor_results, std::make_pair(file_id, settings));
    }
}

bool FileSettings::ExecuteOne(const std::string& event_name, const uint32_t file_id) {
    if (event_name.compare("SET_CURSOR") == 0) {
        bool write_lock(true);
        tbb::queuing_rw_mutex::scoped_lock lock(_cursor_mutex, write_lock);
        FileSettings::cursor_iter cursor_results = _latest_cursor.find(file_id);
        if (cursor_results != _latest_cursor.end()) {
            auto cursor_info = cursor_results->second;
            CARTA::SetCursor message(cursor_info.first);
            uint32_t request_id(cursor_info.second);
            _latest_cursor.unsafe_erase(cursor_results); // remove after retrieve settings
            lock.release();
            _session->OnSetCursor(message, request_id);
            return true;
        } // if no setting for this file id, do nothing
    }
    return false;
}

void FileSettings::ClearSettings(const uint32_t file_id) {
    bool write_lock(true);
    tbb::queuing_rw_mutex::scoped_lock cursor_lock(_cursor_mutex, write_lock);
    _latest_cursor.unsafe_erase(file_id);
}
