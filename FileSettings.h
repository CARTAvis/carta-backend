//# FileSettings.h: uses tbb::concurrent_unordered_map to keep latest per-file settings
//# Used for cursor and view settings

#ifndef CARTA_BACKEND__FILESETTINGS_H_
#define CARTA_BACKEND__FILESETTINGS_H_

#include <mutex>
#include <utility>

#include <tbb/concurrent_unordered_map.h>
#include <tbb/queuing_rw_mutex.h>

#include <carta-protobuf/set_cursor.pb.h>
#include <carta-protobuf/set_image_view.pb.h>

class Session;

class FileSettings {
public:
    FileSettings(Session* session);

    void AddViewSetting(const CARTA::SetImageView& message, uint32_t request_id);
    void AddCursorSetting(const CARTA::SetCursor& message, uint32_t request_id);
    // TODO: change to event type
    bool ExecuteOne(const std::string& event_name, const uint32_t file_id);
    void ClearSettings(const uint32_t file_id);

private:
    Session* _session;
    tbb::queuing_rw_mutex _view_mutex, _cursor_mutex;

    // pair is <message, requestId)
    using view_info_t = std::pair<CARTA::SetImageView, uint32_t>;
    using view_iter = tbb::concurrent_unordered_map<int, view_info_t>::iterator;
    // map is <fileId, view info>
    tbb::concurrent_unordered_map<int, view_info_t> _latest_view;

    // pair is <message, requestId)
    using cursor_info_t = std::pair<CARTA::SetCursor, uint32_t>;
    using cursor_iter = tbb::concurrent_unordered_map<uint32_t, cursor_info_t>::iterator;
    // map is <fileId, cursor info>
    tbb::concurrent_unordered_map<uint32_t, cursor_info_t> _latest_cursor;
};

#endif // CARTA_BACKEND__FILESETTINGS_H_
