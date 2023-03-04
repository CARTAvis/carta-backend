/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEGENERATORS_PVPREVIEWCUT_H_
#define CARTA_BACKEND_IMAGEGENERATORS_PVPREVIEWCUT_H_

#include "Region/Region.h"
#include "Util/File.h"

namespace carta {

// Save PV settings for preview updates.
// Downsampled cube settings are saved in PvPreviewCube.

struct PvPreviewCut {
    // PV cut settings, in source image
    int file_id;
    int region_id;
    int width;

    // PV image axis order
    // Not related to pv cut, but is per-preview so store here for updates
    bool reverse;

    PvPreviewCut() : region_id(0) {}

    PvPreviewCut(int file_id_, int region_id_, int width_, bool reverse_)
        : file_id(file_id_), region_id(region_id_), width(width_), reverse(reverse_) {}

    void UpdateSettings(int file_id_, int region_id_, int width_, bool reverse_) {
        // TODO: check if any changed?
        file_id = file_id_;
        region_id = region_id_;
        width = width_;
        reverse = reverse_;
    }
};

} // namespace carta

#endif // CARTA_BACKEND_IMAGEGENERATORS_PVPREVIEWCUT_H_
