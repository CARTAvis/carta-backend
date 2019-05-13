#include "FileSettings.h"
#include "Session.h"

FileSettings::FileSettings(Session* session) : _session(session) {}

void FileSettings::AddViewSetting(const CARTA::SetImageView& message, uint32_t request_id) {
    FileSettings::view_info_t settings = std::make_pair(message, request_id);
    uint32_t file_id(message.file_id());
    bool write_lock(false); // concurrency okay
    tbb::queuing_rw_mutex::scoped_lock lock(_view_mutex, write_lock);
    FileSettings::view_iter view_results = _latest_view.find(file_id);

    if (view_results != _latest_view.end()) { // replace with new settings
        view_results->second = settings;
    } else {
        _latest_view.insert(view_results, std::make_pair(file_id, settings));
    }
}

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
    if (event_name.compare("SET_IMAGE_VIEW") == 0) {
        bool write_lock(true);
        tbb::queuing_rw_mutex::scoped_lock lock(_view_mutex, write_lock);
        FileSettings::view_iter view_results = _latest_view.find(file_id);

        if (view_results != _latest_view.end()) {
            auto view_info = view_results->second;
            CARTA::SetImageView message(view_info.first);
            _latest_view.unsafe_erase(view_results); // remove after retrieve settings
            lock.release();
            _session->OnSetImageView(message);
            return true;
        } // if no setting for this file id, do nothing
    } else if (event_name.compare("SET_CURSOR") == 0) {
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
    tbb::queuing_rw_mutex::scoped_lock view_lock(_view_mutex, write_lock);
    _latest_view.unsafe_erase(file_id);
    tbb::queuing_rw_mutex::scoped_lock cursor_lock(_cursor_mutex, write_lock);
    _latest_cursor.unsafe_erase(file_id);
}
