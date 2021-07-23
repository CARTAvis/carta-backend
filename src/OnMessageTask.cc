/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "OnMessageTask.h"

#include <algorithm>
#include <cstring>

#include "EventHeader.h"
#include "Logger/Logger.h"
#include "Util.h"

tbb::task* MultiMessageTask::execute() {
    switch (_header.type) {
        case CARTA::EventType::SET_SPATIAL_REQUIREMENTS: {
            CARTA::SetSpatialRequirements message;
            if (message.ParseFromArray(_event_buffer, _event_length)) {
                _session->OnSetSpatialRequirements(message);
            } else {
                spdlog::warn("Bad SET_SPATIAL_REQUIREMENTS message!");
            }
            break;
        }
        case CARTA::EventType::SET_STATS_REQUIREMENTS: {
            CARTA::SetStatsRequirements message;
            if (message.ParseFromArray(_event_buffer, _event_length)) {
                _session->OnSetStatsRequirements(message);
            } else {
                spdlog::warn("Bad SET_STATS_REQUIREMENTS message!");
            }
            break;
        }
        case CARTA::EventType::MOMENT_REQUEST: {
            CARTA::MomentRequest message;
            if (message.ParseFromArray(_event_buffer, _event_length)) {
                _session->OnMomentRequest(message, _header.request_id);
            } else {
                spdlog::warn("Bad MOMENT_REQUEST message!");
            }
            break;
        }
        case CARTA::EventType::FILE_LIST_REQUEST: {
            CARTA::FileListRequest message;
            if (message.ParseFromArray(_event_buffer, _event_length)) {
                _session->OnFileListRequest(message, _header.request_id);
            } else {
                spdlog::warn("Bad FILE_LIST_REQUEST message!");
            }
            break;
        }
        case CARTA::EventType::REGION_LIST_REQUEST: {
            CARTA::RegionListRequest message;
            if (message.ParseFromArray(_event_buffer, _event_length)) {
                _session->OnRegionListRequest(message, _header.request_id);
            } else {
                spdlog::warn("Bad REGION_LIST_REQUEST message!");
            }
            break;
        }
        case CARTA::EventType::CATALOG_LIST_REQUEST: {
            CARTA::CatalogListRequest message;
            if (message.ParseFromArray(_event_buffer, _event_length)) {
                _session->OnCatalogFileList(message, _header.request_id);
            } else {
                spdlog::warn("Bad CATALOG_LIST_REQUEST message!");
            }
            break;
        }
        default: {
            spdlog::warn("Bad event type in MultiMessageType:execute : ({})", _header.type);
            break;
        }
    }

    return nullptr;
}

tbb::task* SetImageChannelsTask::execute() {
    std::pair<CARTA::SetImageChannels, uint32_t> request_pair;
    bool tester;

    _session->ImageChannelLock(fileId);
    tester = _session->_set_channel_queues[fileId].try_pop(request_pair);
    _session->ImageChannelTaskSetIdle(fileId);
    _session->ImageChannelUnlock(fileId);

    if (tester) {
        _session->ExecuteSetChannelEvt(request_pair);
    }

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
        if (_session->CalculateAnimationFlowWindow() > _session->CurrentFlowWindowSize()) {
            _session->SetWaitingTask(true);
        } else {
            increment_ref_count();
            recycle_as_safe_continuation();
        }
    } else {
        if (!_session->WaitingFlowEvent()) {
            _session->CancelAnimation();
        }
    }

    return nullptr;
}

tbb::task* OnAddRequiredTilesTask::execute() {
    _session->OnAddRequiredTiles(_message, _session->AnimationRunning());
    return nullptr;
}

tbb::task* OnSetContourParametersTask::execute() {
    _session->OnSetContourParameters(_message);
    return nullptr;
}

tbb::task* RegionDataStreamsTask::execute() {
    _session->RegionDataStreams(_file_id, _region_id);
    return nullptr;
}

tbb::task* SpectralProfileTask::execute() {
    _session->SendSpectralProfileData(_file_id, _region_id);
    return nullptr;
}

tbb::task* OnSplataloguePingTask::execute() {
    _session->OnSplataloguePing(_request_id);
    return nullptr;
}

tbb::task* OnSpectralLineRequestTask::execute() {
    _session->OnSpectralLineRequest(_message, _request_id);
    return nullptr;
}
