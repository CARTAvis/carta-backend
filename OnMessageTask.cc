#include "OnMessageTask.h"

#include <algorithm>
#include <cstring>

#include <fmt/format.h>

#include "EventHeader.h"
#include "Util.h"

tbb::task* MultiMessageTask::execute() {
    switch (_header.type) {	
	    case CARTA::EventType::FILE_LIST_REQUEST: {
            CARTA::FileListRequest message;
            if (message.ParseFromArray(_event_buffer, _event_length)) {
                _session->OnFileListRequest(message, _header.request_id);
            }
            break;
        }
        case CARTA::EventType::FILE_INFO_REQUEST: {
            CARTA::FileInfoRequest message;
            if (message.ParseFromArray(_event_buffer, _event_length)) {
                _session->OnFileInfoRequest(message, _header.request_id);
            }
            break;
        }
        case CARTA::EventType::OPEN_FILE: {
            CARTA::OpenFile message;
            if (message.ParseFromArray(_event_buffer, _event_length)) {
                _session->OnOpenFile(message, _header.request_id);
            }
            break;
        }
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
        case CARTA::EventType::CLOSE_FILE: {
	    CARTA::CloseFile message;
	    if (message.ParseFromArray(_event_buffer, _event_length)) {
	        _session->CheckCancelAnimationOnFileClose(message.file_id());
		_session->_file_settings.ClearSettings(message.file_id());
		_session->OnCloseFile(message);
	    }
	    break;
	}
        default: {
            std::cerr << " Bad event type in MultiMessageType:execute : (" << this << ") ";
            std::fprintf(stderr, "(%u)\n", _header.type);
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
        increment_ref_count();
        recycle_as_safe_continuation();
    }
	else {
		if (_session->waiting_flow_event()) {
			_session->setWaitingTask_ptr( this );
		}
	}
	
    return nullptr;
}
