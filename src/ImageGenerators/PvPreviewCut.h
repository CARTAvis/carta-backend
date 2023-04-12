/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEGENERATORS_PVPREVIEWCUT_H_
#define CARTA_BACKEND_IMAGEGENERATORS_PVPREVIEWCUT_H_

#include "Region/Region.h"
#include "Util/File.h"

#include <queue>

namespace carta {

// Save PV settings for preview updates.
// Downsampled cube settings are saved in PvPreviewCube.
struct PreviewCutParameters {
    int file_id;
    int region_id;
    int width;
    bool reverse;

    PreviewCutParameters() : file_id(-1), region_id(-1) {}
    PreviewCutParameters(int file_id_, int region_id_, int width_, bool reverse_)
        : file_id(file_id_), region_id(region_id_), width(width_), reverse(reverse_) {}

    bool operator==(const PreviewCutParameters& other) {
        return (HasFileRegionIds(other.file_id, other.region_id) && width == other.width && reverse == other.reverse);
    }

    bool HasFileRegionIds(int file_id_, int region_id_) {
        return ((file_id_ == ALL_FILES || file_id_ == file_id) && (region_id_ == ALL_REGIONS || region_id_ == region_id));
    }
};

class PvPreviewCut {
public:
    PvPreviewCut(const PreviewCutParameters& parameters, const RegionState& region_state);
    ~PvPreviewCut();

    // Preview settings
    bool HasSameParameters(const PreviewCutParameters& parameters);
    bool HasFileRegionIds(int file_id, int region_id);
    int GetWidth();
    int GetReverse();

    // Queued RegionStates for pv cut region
    bool HasQueuedRegion();
    void AddRegion(const RegionState& region_state);
    bool GetNextRegion(RegionState& region_state);
    void ClearRegionQueue();

private:
    // PV cut settings; includes per-preview reverse flag for updates
    PreviewCutParameters _cut_parameters;

    // PV cut region RegionStates (in source image) as the cut is moved.
    // The region states are queued in order, then dequeued to set the region and create each PV preview image.
    // The last region is kept for preview updates when other preview parameters change (not the cut itself).
    std::mutex preview_region_mutex;
    std::queue<RegionState> preview_region_states;
};

} // namespace carta

#endif // CARTA_BACKEND_IMAGEGENERATORS_PVPREVIEWCUT_H_
