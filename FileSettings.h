//# FileSettings.h: uses tbb::concurrent_unordered_map to keep latest per-file settings
//# Used for cursor and view settings

#pragma once

#include "Session.h"
#include <carta-protobuf/set_image_view.pb.h>
#include <carta-protobuf/set_cursor.pb.h>

#include <tbb/concurrent_unordered_map.h>
#include <tbb/queuing_rw_mutex.h>
#include <mutex>
#include <utility>

namespace carta {

class FileSettings  {
public:
    FileSettings(Session *session);

    void addViewSetting(CARTA::SetImageView message, uint32_t requestId);
    void addCursorSetting(CARTA::SetCursor message, uint32_t requestId);
    bool executeOne(const std::string eventName, const uint32_t fileId);
    void clearSettings(const uint32_t fileId);

private:
    Session *session;
    tbb::queuing_rw_mutex viewMutex, cursorMutex;

    // pair is <message, requestId)
    using view_info_t = std::pair<CARTA::SetImageView,uint32_t>;
    using view_iter = tbb::concurrent_unordered_map<int, view_info_t>::iterator;
    // map is <fileId, view info>
    tbb::concurrent_unordered_map<int, view_info_t> latestView;

    // pair is <message, requestId)
    using cursor_info_t = std::pair<CARTA::SetCursor,uint32_t>;
    using cursor_iter = tbb::concurrent_unordered_map<uint32_t, cursor_info_t>::iterator;
    // map is <fileId, cursor info>
    tbb::concurrent_unordered_map<uint32_t, cursor_info_t> latestCursor;
};

} // namespace carta
