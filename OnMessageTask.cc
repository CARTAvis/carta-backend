#include "OnMessageTask.h"

#include <algorithm>
#include <cstring>

#include <fmt/format.h>

#include "EventHeader.h"
#include "Util.h"

// Looks for null termination in a char array to determine event names from message payloads
std::string getEventName(char* rawMessage) {
    static const size_t max_len = 32;
    return std::string(rawMessage, std::min(std::strlen(rawMessage), max_len));
}

unsigned int __num_on_message_tasks = 0;
unsigned int __on_message_tasks_created = 0;

tbb::task* MultiMessageTask::execute() {
    switch (_header._type) {
        case CARTA::EventType::REGISTER_VIEWER: {
            CARTA::RegisterViewer message;
            if (message.ParseFromArray(_event_buffer, _event_length)) {
                session->onRegisterViewer(message, _header._req_id);
            }
            break;
        }
        case CARTA::EventType::FILE_LIST_REQUEST: {
            CARTA::FileListRequest message;
            if (message.ParseFromArray(_event_buffer, _event_length)) {
                session->onFileListRequest(message, _header._req_id);
            }
            break;
        }
        case CARTA::EventType::FILE_INFO_REQUEST: {
            CARTA::FileInfoRequest message;
            if (message.ParseFromArray(_event_buffer, _event_length)) {
                session->onFileInfoRequest(message, _header._req_id);
            }
            break;
        }
        case CARTA::EventType::OPEN_FILE: {
            CARTA::OpenFile message;
            if (message.ParseFromArray(_event_buffer, _event_length)) {
                session->onOpenFile(message, _header._req_id);
            }
            break;
        }
        case CARTA::EventType::CLOSE_FILE: {
            CARTA::CloseFile message;
            if (message.ParseFromArray(_event_buffer, _event_length)) {
                session->fsettings.clearSettings(message.file_id());
                session->onCloseFile(message, _header._req_id);
            }
            break;
        }
        case CARTA::EventType::SET_SPATIAL_REQUIREMENTS: {
            CARTA::SetSpatialRequirements message;
            if (message.ParseFromArray(_event_buffer, _event_length)) {
                session->onSetSpatialRequirements(message, _header._req_id);
            }
            break;
        }
        case CARTA::EventType::SET_SPECTRAL_REQUIREMENTS: {
            CARTA::SetSpectralRequirements message;
            if (message.ParseFromArray(_event_buffer, _event_length)) {
                session->onSetSpectralRequirements(message, _header._req_id);
            }
            break;
        }
        case CARTA::EventType::SET_STATS_REQUIREMENTS: {
            CARTA::SetStatsRequirements message;
            if (message.ParseFromArray(_event_buffer, _event_length)) {
                session->onSetStatsRequirements(message, _header._req_id);
            }
            break;
        }
        case CARTA::EventType::SET_REGION: {
            CARTA::SetRegion message;
            if (message.ParseFromArray(_event_buffer, _event_length)) {
                session->onSetRegion(message, _header._req_id);
            }
            break;
        }
        case CARTA::EventType::REMOVE_REGION: {
            CARTA::RemoveRegion message;
            if (message.ParseFromArray(_event_buffer, _event_length)) {
                session->onRemoveRegion(message, _header._req_id);
            }
            break;
        }
        default: {
            std::cerr << " Bad event type in MultiMessageType:execute : (" << this << ") ";
            std::fprintf(stderr, "(%u)\n", _header._type);
            exit(1);
        }
    }

    return nullptr;
}

tbb::task* SetImageChannelsTask::execute() {
    bool tester;

    session->executeSetChanEvt(_req);
    session->image_channel_lock();
    if (!(tester = session->setchanq.try_pop(_req)))
        session->image_channal_task_set_idle();
    session->image_channel_unlock();

    if (tester) {
        increment_ref_count();
        recycle_as_safe_continuation();
    }

    return nullptr;
}

tbb::task* SetImageViewTask::execute() {
    session->fsettings.executeOne("SET_IMAGE_VIEW", _file_id);
    return nullptr;
}

tbb::task* SetCursorTask::execute() {
    session->fsettings.executeOne("SET_CURSOR", _file_id);
    return nullptr;
}

tbb::task* SetHistogramReqsTask::execute() {
    CARTA::SetHistogramRequirements message;
    if (message.ParseFromArray(_event_buffer, _event_length)) {
        session->onSetHistogramRequirements(message, _header._req_id);
    }

    return nullptr;
}

tbb::task* AnimationTask::execute() {
    if (session->execute_animation_frame()) {
        increment_ref_count();
        recycle_as_safe_continuation();
    }

    return nullptr;
}
