/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEGENERATORS_PVPREVIEWSETTINGS_H_
#define CARTA_BACKEND_IMAGEGENERATORS_PVPREVIEWSETTINGS_H_

#include "Region/Region.h"
#include "Util/File.h"

namespace carta {

// Save PV settings for preview updates.
// Downsampled cube settings saved in PvPreviewCube.

struct PvPreviewSettings {
    // Source image
    int file_id;

    // PV cut settings
    int region_id;
    RegionState region_state;
    int width;

    // Output image axes
    bool reverse;

    PvPreviewSettings() : region_id(0) {}

    PvPreviewSettings(int file_id_, int region_id_, const RegionState& region_state_, int width_, bool reverse_)
        : file_id(file_id_), region_id(region_id_), region_state(region_state_), width(width_), reverse(reverse_) {}

    bool UpdateRegion(int file_id_, int region_id_, const RegionState& region_state_) {
        // Returns true when PV cut was changed from previous state.
        if ((file_id_ != ALL_FILES && file_id != file_id_) || (region_id != region_id_) || (region_state == region_state_)) {
            return false; // wrong file/region id, or did not change state
        }

        region_state = region_state_;
        return true;
    }
};

} // namespace carta

#endif // CARTA_BACKEND_IMAGEGENERATORS_PVPREVIEWSETTINGS_H_
