#include "OnMessageTask.h"

#include <algorithm>
#include <cstring>

#include <fmt/format.h>
#include <fmt/ostream.h>

#include "EventHeader.h"
#include "Util.h"

tbb::task* MultiMessageTask::execute() {
    switch (_header.type) {
        case CARTA::EventType::SET_SPATIAL_REQUIREMENTS: {
            CARTA::SetSpatialRequirements message;
            if (message.ParseFromArray(_event_buffer, _event_length)) {
                _session->OnSetSpatialRequirements(message);
            }
            break;
        }
        case CARTA::EventType::SET_SPECTRAL_REQUIREMENTS: {
            CARTA::SetSpectralRequirements message;
            if (message.ParseFromArray(_event_buffer, _event_length)) {
                _session->OnSetSpectralRequirements(message);
            }
            break;
        }
        case CARTA::EventType::SET_STATS_REQUIREMENTS: {
            CARTA::SetStatsRequirements message;
            if (message.ParseFromArray(_event_buffer, _event_length)) {
                _session->OnSetStatsRequirements(message);
            }
            break;
        }
        case CARTA::EventType::SET_REGION: {
            CARTA::SetRegion message;
            if (message.ParseFromArray(_event_buffer, _event_length)) {
                _session->OnSetRegion(message, _header.request_id);
            }
            break;
        }
        case CARTA::EventType::REMOVE_REGION: {
            CARTA::RemoveRegion message;
            if (message.ParseFromArray(_event_buffer, _event_length)) {
                _session->OnRemoveRegion(message);
            }
            break;
        }
        default: {
            fmt::print("Bad event type in MultiMessageType:execute : ({})", _header.type);
            break;
        }
    }

    return nullptr;
}

tbb::task* SetImageChannelsTask::execute() {
    bool tester;

    _session->ExecuteSetChannelEvt(_request_pair);
    _session->ImageChannelLock();

    if (!(tester = _session->_set_channel_queue.try_pop(_request_pair)))
        _session->ImageChannelTaskSetIdle();
    _session->ImageChannelUnlock();

    if (tester) {
        increment_ref_count();
        recycle_as_safe_continuation();
    }

    return nullptr;
}

tbb::task* SetImageViewTask::execute() {
    _session->_file_settings.ExecuteOne("SET_IMAGE_VIEW", _file_id);
    return nullptr;
}

tbb::task* SetCursorTask::execute() {
    _session->_file_settings.ExecuteOne("SET_CURSOR", _file_id);
    return nullptr;
}

tbb::task* SetHistogramRequirementsTask::execute() {
    CARTA::SetHistogramRequirements message;
    if (message.ParseFromArray(_event_buffer, _event_length)) {
        _session->OnSetHistogramRequirements(message, _header.request_id);
    }

    return nullptr;
}

tbb::task* AnimationTask::execute() {
    if (_session->ExecuteAnimationFrame()) {
        if (_session->calcuteAnimationFlowWindow() > CARTA::AnimationFlowWindowSize) {
            _session->setWaitingTask(true);
        } else {
            increment_ref_count();
            recycle_as_safe_continuation();
        }
    } else {
        if (!_session->waitingFlowEvent()) {
            _session->CancelAnimation();
        }
    }

    return nullptr;
}
