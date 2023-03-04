/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEGENERATORS_PVPREVIEWCUBE_H_
#define CARTA_BACKEND_IMAGEGENERATORS_PVPREVIEWCUBE_H_

#include "Region/Region.h"
#include "Util/File.h"

namespace carta {

struct PvPreviewCube {
    // Cube parameters
    int file_id;
    int region_id;
    AxisRange spectral_range;
    int rebin_xy;
    int rebin_z;
    int stokes;

    // Which previews are using this cube
    std::vector<int> preview_ids;
    // Frame id (key for frames map) for this preview cube
    int frame_id;
    // Flag to stop downsampling image
    bool stop_cube;

    PvPreviewCube() : file_id(-1) {}
    PvPreviewCube(int file_id_, int region_id_, const AxisRange& spectral_range_, int rebin_xy_, int rebin_z_, int stokes_, int preview_id_)
        : file_id(file_id_),
          region_id(region_id_),
          spectral_range(spectral_range_),
          rebin_xy(rebin_xy_),
          rebin_z(rebin_z_),
          stokes(stokes_),
          stop_cube(false) {
        AddPreviewId(preview_id_);
    }

    bool operator==(const PvPreviewCube& other) {
        return ((other.file_id == ALL_FILES || file_id == other.file_id) && (region_id == other.region_id) &&
                (spectral_range == other.spectral_range) && (rebin_xy == other.rebin_xy) && (rebin_z == other.rebin_z) &&
                (stokes == other.stokes));
    }

    bool IsSet() {
        return file_id >= 0;
    }

    void StopCube() {
        stop_cube = true;
    }

    // ********************************************************************
    // Manage preview IDs
    void AddPreviewId(int preview_id) {
        if (!HasPreviewId(preview_id)) {
            preview_ids.push_back(preview_id);
        }
    }

    void RemovePreviewId(int preview_id) {
        for (auto it = preview_ids.begin(); it != preview_ids.end(); ++it) {
            if (*it == preview_id) {
                preview_ids.erase(it);
                break;
            }
        }
    }

    bool NoPreviewIds() {
        return preview_ids.size() == 0;
    }

    bool HasPreviewId(int preview_id) {
        for (auto it = preview_ids.begin(); it != preview_ids.end(); ++it) {
            if (*it == preview_id) {
                return true;
            }
        }
        return false;
    }
    // ********************************************************************
};

} // namespace carta

#endif // CARTA_BACKEND_IMAGEGENERATORS_PVPREVIEWCUBE_H_
