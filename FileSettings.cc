#include "FileSettings.h"

using namespace carta;

FileSettings::FileSettings(Session *session_)
    : session(session_)
{}

void FileSettings::addViewSetting(CARTA::SetImageView message, uint32_t requestId) {
    FileSettings::view_info_t settings = std::make_pair(message, requestId);
    uint32_t fileId(message.file_id());
    bool writeLock(false);  // concurrency okay 
    tbb::queuing_rw_mutex::scoped_lock lock(viewMutex, writeLock);
    FileSettings::view_iter viter = latestView.find(fileId);
    if (viter != latestView.end()) {  // replace with new settings
        viter->second = settings;
    } else {
        latestView.insert(viter, std::make_pair(fileId, settings));
    }
}

void FileSettings::addCursorSetting(CARTA::SetCursor message, uint32_t requestId) {
    FileSettings::cursor_info_t settings = std::make_pair(message, requestId);
    uint32_t fileId(message.file_id());
    bool writeLock(false);  // concurrency okay 
    tbb::queuing_rw_mutex::scoped_lock lock(cursorMutex, writeLock);
    FileSettings::cursor_iter citer = latestCursor.find(fileId);
    if (citer != latestCursor.end()) {  // replace with new settings
        citer->second = settings;
    } else {
        latestCursor.insert(citer, std::make_pair(fileId, settings));
    }
}

bool FileSettings::executeOne(const std::string eventName, const uint32_t fileId) {
    if (eventName.compare("SET_IMAGE_VIEW") == 0) {
        // if no setting for this file id, do nothing
        FileSettings::view_iter viter = latestView.find(fileId);
        if (viter != latestView.end()) {
            bool writeLock(true);
            tbb::queuing_rw_mutex::scoped_lock lock(cursorMutex, writeLock);
            auto viewInfo = viter->second;
            session->onSetImageView(viewInfo.first, viewInfo.second);
            latestView.unsafe_erase(viter); // remove after use
            return true;
        }
    } else if (eventName.compare("SET_CURSOR") == 0) {
        // if no setting for this file id, do nothing
        FileSettings::cursor_iter citer = latestCursor.find(fileId);
        if (citer != latestCursor.end()) {
            bool writeLock(true);
            tbb::queuing_rw_mutex::scoped_lock lock(cursorMutex, writeLock);
            auto cursorInfo = citer->second;
            session->onSetCursor(cursorInfo.first, cursorInfo.second);
            latestCursor.unsafe_erase(citer);  // remove after use
            return true;
        }
    }
    return false;
}
