#include "OnMessageTask.h"
#include "util.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fmt/format.h>

// Looks for null termination in a char array to determine event names from message payloads
std::string getEventName(char* rawMessage) {
    static const size_t max_len = 32;
    return std::string(rawMessage, std::min(std::strlen(rawMessage), max_len));
}

OnMessageTask::OnMessageTask(std::string uuid_, Session *session_,
                             tbb::concurrent_queue<std::tuple<std::string,uint32_t,std::vector<char>>> *mqueue_,
                             carta::AnimationQueue *aqueue_)
    : uuid(uuid_),
      session(session_),
      mqueue(mqueue_),
      aqueue(aqueue_)
{}

tbb::task* OnMessageTask::execute() {
    //CARTA ICD
    auto tStart = std::chrono::high_resolution_clock::now();
    std::tuple<std::string,uint32_t,std::vector<char>> msg;
    mqueue->try_pop(msg);
    std::string eventName;
    uint32_t requestId;
    std::vector<char> eventPayload;
    std::tie(eventName, requestId, eventPayload) = msg;
    log(uuid, "Processing operation {}", eventName);
    if (eventName == "REGISTER_VIEWER") {
        CARTA::RegisterViewer message;
        if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
            session->onRegisterViewer(message, requestId);
        }
    } else if (eventName == "FILE_LIST_REQUEST") {
        CARTA::FileListRequest message;
        if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
            session->onFileListRequest(message, requestId);
        }
    } else if (eventName == "FILE_INFO_REQUEST") {
        CARTA::FileInfoRequest message;
        if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
            session->onFileInfoRequest(message, requestId);
        }
    } else if (eventName == "OPEN_FILE") {
        CARTA::OpenFile message;
        if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
            session->onOpenFile(message, requestId);
        }
    } else if (eventName == "CLOSE_FILE") {
        CARTA::CloseFile message;
        if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
            session->onCloseFile(message, requestId);
        }
    } else if (eventName == "SET_IMAGE_VIEW") {
        CARTA::SetImageView message;
        if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
            session->onSetImageView(message, requestId);
        }
    } else if (eventName == "SET_IMAGE_CHANNELS") {
        CARTA::SetImageChannels message;
        if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
            aqueue->executeOne();
        }
    } else if (eventName == "SET_CURSOR") {
        CARTA::SetCursor message;
        if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
            session->onSetCursor(message, requestId);
        }
    } else if (eventName == "SET_SPATIAL_REQUIREMENTS") {
        CARTA::SetSpatialRequirements message;
        if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
            session->onSetSpatialRequirements(message, requestId);
        }
    } else if (eventName == "SET_HISTOGRAM_REQUIREMENTS") {
        CARTA::SetHistogramRequirements message;
        if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
            session->onSetHistogramRequirements(message, requestId);
        }
    } else if (eventName == "SET_SPECTRAL_REQUIREMENTS") {
        CARTA::SetSpectralRequirements message;
        if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
            session->onSetSpectralRequirements(message, requestId);
        }
    } else if (eventName == "SET_STATS_REQUIREMENTS") {
        CARTA::SetStatsRequirements message;
        if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
            session->onSetStatsRequirements(message, requestId);
        }
    } else if (eventName == "SET_REGION") {
        CARTA::SetRegion message;
        if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
            session->onSetRegion(message, requestId);
        }
    } else if (eventName == "REMOVE_REGION") {
        CARTA::RemoveRegion message;
        if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
            session->onRemoveRegion(message, requestId);
        }
    } else {
        log(uuid, "Unknown event type {}", eventName);
    }
    auto tEnd = std::chrono::high_resolution_clock::now();
    auto dt = std::chrono::duration_cast<std::chrono::microseconds>(tEnd - tStart).count();
    log(uuid, "Operation {} took {}ms", eventName, dt/1e3);
    return nullptr;
}
