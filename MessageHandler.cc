#include "MessageHandler.h"

MessageHandler::MessageHandler(Session* session, char* raw_message, size_t length) : _session(session) {
    if (length > sizeof(carta::EventHeader)) {
        _header = *reinterpret_cast<carta::EventHeader*>(raw_message);
        _event_buf = raw_message + sizeof(carta::EventHeader);
        _event_length = length - sizeof(carta::EventHeader);
        Execute();
    } else {
        fmt::print("Bad event header!\n");
    }
}

void MessageHandler::Execute() {
    switch (_header.type) {
        case CARTA::EventType::REGISTER_VIEWER: {
            CARTA::RegisterViewer message;
            if (message.ParseFromArray(_event_buf, _event_length)) {
                Command(message);
            } else {
                fmt::print("Bad REGISTER_VIEWER message!\n");
            }
            break;
        }
        case CARTA::EventType::SET_IMAGE_CHANNELS: {
            CARTA::SetImageChannels message;
            if (message.ParseFromArray(_event_buf, _event_length)) {
                Command(message);
            } else {
                fmt::print("Bad SET_IMAGE_CHANNELS message!\n");
            }
            break;
        }
        case CARTA::EventType::SET_IMAGE_VIEW: {
            CARTA::SetImageView message;
            if (message.ParseFromArray(_event_buf, _event_length)) {
                Command(message);
            } else {
                fmt::print("Bad SET_IMAGE_VIEW message!\n");
            }
            break;
        }
        case CARTA::EventType::SET_CURSOR: {
            CARTA::SetCursor message;
            if (message.ParseFromArray(_event_buf, _event_length)) {
                Command(message);
            } else {
                fmt::print("Bad SET_CURSOR message!\n");
            }
            break;
        }
        case CARTA::EventType::SET_HISTOGRAM_REQUIREMENTS: {
            CARTA::SetHistogramRequirements message;
            if (message.ParseFromArray(_event_buf, _event_length)) {
                Command(message);
            } else {
                fmt::print("Bad SET_HISTOGRAM_REQUIREMENTS message!\n");
            }
            break;
        }
        case CARTA::EventType::CLOSE_FILE: {
            CARTA::CloseFile message;
            if (message.ParseFromArray(_event_buf, _event_length)) {
                Command(message);
            } else {
                fmt::print("Bad CLOSE_FILE message!\n");
            }
            break;
        }
        case CARTA::EventType::START_ANIMATION: {
            CARTA::StartAnimation message;
            if (message.ParseFromArray(_event_buf, _event_length)) {
                Command(message);
            } else {
                fmt::print("Bad START_ANIMATION message!\n");
            }
            break;
        }
        case CARTA::EventType::STOP_ANIMATION: {
            CARTA::StopAnimation message;
            if (message.ParseFromArray(_event_buf, _event_length)) {
                Command(message);
            } else {
                fmt::print("Bad STOP_ANIMATION message!\n");
            }
            break;
        }
        case CARTA::EventType::ANIMATION_FLOW_CONTROL: {
            CARTA::AnimationFlowControl message;
            if (message.ParseFromArray(_event_buf, _event_length)) {
                Command(message);
            } else {
                fmt::print("Bad ANIMATION_FLOW_CONTROL message!\n");
            }
            break;
        }
        case CARTA::EventType::FILE_INFO_REQUEST: {
            CARTA::FileInfoRequest message;
            if (message.ParseFromArray(_event_buf, _event_length)) {
                Command(message);
            } else {
                fmt::print("Bad FILE_INFO_REQUEST message!\n");
            }
            break;
        }
        case CARTA::EventType::FILE_LIST_REQUEST: {
            CARTA::FileListRequest message;
            if (message.ParseFromArray(_event_buf, _event_length)) {
                Command(message);
            } else {
                fmt::print("Bad FILE_LIST_REQUEST message!\n");
            }
            break;
        }
        case CARTA::EventType::OPEN_FILE: {
            CARTA::OpenFile message;
            if (message.ParseFromArray(_event_buf, _event_length)) {
                Command(message);
            } else {
                fmt::print("Bad OPEN_FILE message!\n");
            }
            break;
        }
        case CARTA::EventType::ADD_REQUIRED_TILES: {
            CARTA::AddRequiredTiles message;
            if (message.ParseFromArray(_event_buf, _event_length)) {
                Command(message);
            } else {
                fmt::print("Bad ADD_REQUIRED_TILES message!\n");
            }
            break;
        }
        case CARTA::EventType::REGION_LIST_REQUEST: {
            CARTA::RegionListRequest message;
            if (message.ParseFromArray(_event_buf, _event_length)) {
                Command(message);
            } else {
                fmt::print("Bad REGION_LIST_REQUEST message!\n");
            }
            break;
        }
        case CARTA::EventType::REGION_FILE_INFO_REQUEST: {
            CARTA::RegionFileInfoRequest message;
            if (message.ParseFromArray(_event_buf, _event_length)) {
                Command(message);
            } else {
                fmt::print("Bad REGION_FILE_INFO_REQUEST message!\n");
            }
            break;
        }
        case CARTA::EventType::IMPORT_REGION: {
            CARTA::ImportRegion message;
            if (message.ParseFromArray(_event_buf, _event_length)) {
                Command(message);
            } else {
                fmt::print("Bad IMPORT_REGION message!\n");
            }
            break;
        }
        case CARTA::EventType::EXPORT_REGION: {
            CARTA::ExportRegion message;
            if (message.ParseFromArray(_event_buf, _event_length)) {
                Command(message);
            } else {
                fmt::print("Bad EXPORT_REGION message!\n");
            }
            break;
        }
        case CARTA::EventType::SET_SPATIAL_REQUIREMENTS: {
            CARTA::SetSpatialRequirements message;
            if (message.ParseFromArray(_event_buf, _event_length)) {
                Command(message);
            } else {
                fmt::print("Bad SET_SPATIAL_REQUIREMENTS message!\n");
            }
            break;
        }
        case CARTA::EventType::SET_SPECTRAL_REQUIREMENTS: {
            CARTA::SetSpectralRequirements message;
            if (message.ParseFromArray(_event_buf, _event_length)) {
                Command(message);
            } else {
                fmt::print("Bad SET_SPECTRAL_REQUIREMENTS message!\n");
            }
            break;
        }
        case CARTA::EventType::SET_STATS_REQUIREMENTS: {
            CARTA::SetStatsRequirements message;
            if (message.ParseFromArray(_event_buf, _event_length)) {
                Command(message);
            } else {
                fmt::print("Bad SET_STATS_REQUIREMENTS message!\n");
            }
            break;
        }
        case CARTA::EventType::SET_REGION: {
            CARTA::SetRegion message;
            if (message.ParseFromArray(_event_buf, _event_length)) {
                Command(message);
            } else {
                fmt::print("Bad SET_REGION message!\n");
            }
            break;
        }
        case CARTA::EventType::REMOVE_REGION: {
            CARTA::RemoveRegion message;
            if (message.ParseFromArray(_event_buf, _event_length)) {
                Command(message);
            } else {
                fmt::print("Bad REMOVE_REGION message!\n");
            }
            break;
        }
        default: {
            fmt::print("Bad event type : {}!\n", _header.type);
        }
    }
}

void MessageHandler::Command(CARTA::RegisterViewer message) {
    _session->OnRegisterViewer(message, _header.icd_version, _header.request_id);
}

void MessageHandler::Command(CARTA::SetImageChannels message) {
    _session->ImageChannelLock();
    if (!_session->ImageChannelTaskTestAndSet()) {
        OnMessageTask* tsk = new (tbb::task::allocate_root(_session->Context())) SetImageChannelsTask(_session);
        tbb::task::enqueue(*tsk);
    }
    // has its own queue to keep channels in order during animation
    _session->AddToSetChannelQueue(message, _header.request_id);
    _session->ImageChannelUnlock();
}

void MessageHandler::Command(CARTA::SetImageView message) {
    _session->OnSetImageView(message);
}

void MessageHandler::Command(CARTA::SetCursor message) {
    _session->AddCursorSetting(message, _header.request_id);
    OnMessageTask* tsk = new (tbb::task::allocate_root(_session->Context())) SetCursorTask(_session, message.file_id());
    tbb::task::enqueue(*tsk);
}

void MessageHandler::Command(CARTA::SetHistogramRequirements message) {
    if (message.histograms_size() == 0) {
        _session->CancelSetHistRequirements();
    } else {
        _session->ResetHistContext();
        OnMessageTask* tsk =
            new (tbb::task::allocate_root(_session->HistContext())) SetHistogramRequirementsTask(_session, message, _header);
        tbb::task::enqueue(*tsk);
    }
}

void MessageHandler::Command(CARTA::CloseFile message) {
    _session->CheckCancelAnimationOnFileClose(message.file_id());
    _session->_file_settings.ClearSettings(message.file_id());
    _session->OnCloseFile(message);
}

void MessageHandler::Command(CARTA::StartAnimation message) {
    _session->CancelExistingAnimation();
    _session->BuildAnimationObject(message, _header.request_id);
    OnMessageTask* tsk = new (tbb::task::allocate_root(_session->AnimationContext())) AnimationTask(_session);
    tbb::task::enqueue(*tsk);
}

void MessageHandler::Command(CARTA::StopAnimation message) {
    _session->StopAnimation(message.file_id(), message.end_frame());
}

void MessageHandler::Command(CARTA::AnimationFlowControl message) {
    _session->HandleAnimationFlowControlEvt(message);
}

void MessageHandler::Command(CARTA::FileInfoRequest message) {
    _session->OnFileInfoRequest(message, _header.request_id);
}

void MessageHandler::Command(CARTA::FileListRequest message) {
    _session->OnFileListRequest(message, _header.request_id);
}

void MessageHandler::Command(CARTA::OpenFile message) {
    _session->OnOpenFile(message, _header.request_id);
}

void MessageHandler::Command(CARTA::AddRequiredTiles message) {
    OnMessageTask* tsk = new (tbb::task::allocate_root(_session->Context())) OnAddRequiredTilesTask(_session, message);
    tbb::task::enqueue(*tsk);
}

void MessageHandler::Command(CARTA::RegionListRequest message) {
    _session->OnRegionListRequest(message, _header.request_id);
}

void MessageHandler::Command(CARTA::RegionFileInfoRequest message) {
    _session->OnRegionFileInfoRequest(message, _header.request_id);
}

void MessageHandler::Command(CARTA::ImportRegion message) {
    _session->OnImportRegion(message, _header.request_id);
}

void MessageHandler::Command(CARTA::ExportRegion message) {
    _session->OnExportRegion(message, _header.request_id);
}

void MessageHandler::Command(CARTA::SetSpatialRequirements message) {
    OnMessageTask* tsk = new (tbb::task::allocate_root(_session->Context())) SetSpatialRequirementsTask(_session, message);
    tbb::task::enqueue(*tsk);
}

void MessageHandler::Command(CARTA::SetSpectralRequirements message) {
    OnMessageTask* tsk = new (tbb::task::allocate_root(_session->Context())) SetSpectralRequirementsTask(_session, message);
    tbb::task::enqueue(*tsk);
}

void MessageHandler::Command(CARTA::SetStatsRequirements message) {
    OnMessageTask* tsk = new (tbb::task::allocate_root(_session->Context())) SetStatsRequirementsTask(_session, message);
    tbb::task::enqueue(*tsk);
}

void MessageHandler::Command(CARTA::SetRegion message) {
    OnMessageTask* tsk = new (tbb::task::allocate_root(_session->Context())) SetRegionTask(_session, message, _header);
    tbb::task::enqueue(*tsk);
}

void MessageHandler::Command(CARTA::RemoveRegion message) {
    OnMessageTask* tsk = new (tbb::task::allocate_root(_session->Context())) RemoveRegionTask(_session, message);
    tbb::task::enqueue(*tsk);
}