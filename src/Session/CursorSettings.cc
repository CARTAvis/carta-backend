/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "CursorSettings.h"

using namespace carta;

void CursorSettings::AddCursorSetting(const CARTA::SetCursor& message, uint32_t request_id) {
    CursorSettings::cursor_info_t settings = std::make_pair(message, request_id);
    uint32_t file_id(message.file_id());
    bool write_lock(true); // With TBB this was false. Need to determine if this change
                           // has a significant performance hit.
    queuing_rw_mutex_scoped lock(&_cursor_mutex, write_lock);

    CursorSettings::cursor_iter cursor_results = _latest_cursor.find(file_id);
    if (cursor_results != _latest_cursor.end()) { // replace with new settings
        cursor_results->second = settings;
    } else {
        _latest_cursor.insert(cursor_results, std::make_pair(file_id, settings));
    }
}

bool CursorSettings::ExecuteOne(const uint32_t file_id, const std::function<void (const CARTA::SetCursor&, uint32_t)>& callback) {
    bool write_lock(true);
    queuing_rw_mutex_scoped lock(&_cursor_mutex, write_lock);
    CursorSettings::cursor_iter cursor_results = _latest_cursor.find(file_id);
    if (cursor_results != _latest_cursor.end()) {
        auto cursor_info = cursor_results->second;
        CARTA::SetCursor message(cursor_info.first);
        uint32_t request_id(cursor_info.second);
        _latest_cursor.erase(cursor_results); // remove after retrieve settings
        lock.release();
        callback(message, request_id);
        return true;
    } // if no setting for this file id, do nothing
    return false;
}

void CursorSettings::ClearSettings(const uint32_t file_id) {
    bool write_lock(true);
    queuing_rw_mutex_scoped cursor_lock(&_cursor_mutex, write_lock);
    _latest_cursor.erase(file_id);
}
