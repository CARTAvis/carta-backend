/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "PvPreviewCut.h"

namespace carta {

PvPreviewCut::PvPreviewCut(const PreviewCutParameters& parameters, const RegionState& region_state) : _cut_parameters(parameters) {
    AddRegion(region_state);
}

PvPreviewCut::~PvPreviewCut() {
    ClearRegionQueue();
}

bool PvPreviewCut::HasSameParameters(const PreviewCutParameters& parameters) {
    return _cut_parameters == parameters;
}

bool PvPreviewCut::HasFileRegionIds(int file_id, int region_id) {
    // Check if file and region ids are for this cut
    return _cut_parameters.HasFileRegionIds(file_id, region_id);
}

int PvPreviewCut::GetWidth() {
    return _cut_parameters.width;
}

int PvPreviewCut::GetReverse() {
    return _cut_parameters.reverse;
}

bool PvPreviewCut::HasQueuedRegion() {
    std::lock_guard<std::mutex> guard(preview_region_mutex);
    return (!preview_region_states.empty());
}

void PvPreviewCut::AddRegion(const RegionState& region_state) {
    std::lock_guard<std::mutex> guard(preview_region_mutex);
    preview_region_states.push(region_state);
}

bool PvPreviewCut::GetNextRegion(RegionState& region_state) {
    // Remove and return next region state in queue.
    // Returns false if no more regions.
    std::lock_guard<std::mutex> guard(preview_region_mutex);
    if (preview_region_states.empty()) {
        return false;
    }
    region_state = preview_region_states.front();
    preview_region_states.pop();
    return true;
}

void PvPreviewCut::ClearRegionQueue() {
    std::lock_guard<std::mutex> guard(preview_region_mutex);
    while (!preview_region_states.empty()) {
        preview_region_states.pop();
    }
}

} // namespace carta
