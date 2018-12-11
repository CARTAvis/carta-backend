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
        bool writeLock(true);
        tbb::queuing_rw_mutex::scoped_lock lock(viewMutex, writeLock);
        FileSettings::view_iter viter = latestView.find(fileId);
        if (viter != latestView.end()) {
            auto viewInfo = viter->second;
            CARTA::SetImageView message(viewInfo.first);
            uint32_t requestId(viewInfo.second);
            latestView.unsafe_erase(viter); // remove after retrieve settings
            lock.release();
            session->onSetImageView(message, requestId);
            return true;
        } // if no setting for this file id, do nothing
    } else if (eventName.compare("SET_CURSOR") == 0) {
        bool writeLock(true);
        tbb::queuing_rw_mutex::scoped_lock lock(cursorMutex, writeLock);
        FileSettings::cursor_iter citer = latestCursor.find(fileId);
        if (citer != latestCursor.end()) {
            auto cursorInfo = citer->second;
            CARTA::SetCursor message(cursorInfo.first);
            uint32_t requestId(cursorInfo.second);
            latestCursor.unsafe_erase(citer);  // remove after retrieve settings
            lock.release();
            session->onSetCursor(message, requestId);
            return true;
        } // if no setting for this file id, do nothing
    }
    return false;
}

void FileSettings::clearSettings(const uint32_t fileId) {
    bool writeLock(true);
    tbb::queuing_rw_mutex::scoped_lock viewlock(viewMutex, writeLock);
    latestView.unsafe_erase(fileId);
    tbb::queuing_rw_mutex::scoped_lock cursorlock(cursorMutex, writeLock);
    latestCursor.unsafe_erase(fileId);
}
    

