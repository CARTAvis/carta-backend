/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__ONMESSAGETASK_TCC_
#define CARTA_BACKEND__ONMESSAGETASK_TCC_

namespace carta {

template <typename T>
class GeneralMessageTask : public OnMessageTask {
    OnMessageTask* execute() {
        if constexpr (std::is_same_v<T, CARTA::SetHistogramRequirements>) {
            _session->OnSetHistogramRequirements(_message, _request_id);
        } else if constexpr (std::is_same_v<T, CARTA::AddRequiredTiles>) {
            _session->OnAddRequiredTiles(_message, _session->AnimationRunning());
        } else if constexpr (std::is_same_v<T, CARTA::SetContourParameters>) {
            _session->OnSetContourParameters(_message);
        } else if constexpr (std::is_same_v<T, CARTA::SpectralLineRequest>) {
            _session->OnSpectralLineRequest(_message, _request_id);
        } else if constexpr (std::is_same_v<T, CARTA::SetSpatialRequirements>) {
            _session->OnSetSpatialRequirements(_message);
        } else if constexpr (std::is_same_v<T, CARTA::SetStatsRequirements>) {
            _session->OnSetStatsRequirements(_message);
        } else if constexpr (std::is_same_v<T, CARTA::MomentRequest>) {
            _session->OnMomentRequest(_message, _request_id);
        } else if constexpr (std::is_same_v<T, CARTA::FileListRequest>) {
            _session->OnFileListRequest(_message, _request_id);
        } else if constexpr (std::is_same_v<T, CARTA::RegionListRequest>) {
            _session->OnRegionListRequest(_message, _request_id);
        } else if constexpr (std::is_same_v<T, CARTA::CatalogListRequest>) {
            _session->OnCatalogFileList(_message, _request_id);
        } else if constexpr (std::is_same_v<T, CARTA::PvRequest>) {
            _session->OnPvRequest(_message, _request_id);
        } else if constexpr (std::is_same_v<T, CARTA::FittingRequest>) {
            _session->OnFittingRequest(_message, _request_id);
        } else if constexpr (std::is_same_v<T, CARTA::SetVectorOverlayParameters>) {
            _session->OnSetVectorOverlayParameters(_message);
        } else {
            spdlog::warn("Bad event type for GeneralMessageTask!");
        }
        return nullptr;
    };

    T _message;
    uint32_t _request_id;

public:
    GeneralMessageTask(Session* session, T message, uint32_t request_id)
        : OnMessageTask(session), _message(message), _request_id(request_id) {}
    ~GeneralMessageTask() = default;
};

} // namespace carta

#endif // CARTA_BACKEND__ONMESSAGETASK_TCC_
