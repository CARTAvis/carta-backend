/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__FRAME_IMAGECACHE_H_
#define CARTA_BACKEND__FRAME_IMAGECACHE_H_

#include "Util/Image.h"

#include <casacore/casa/Arrays/IPosition.h>
#include <casacore/lattices/Lattices/ArrayLattice.h>

namespace carta {

struct ImageCache {
    size_t width;
    size_t height;
    size_t depth;
    size_t num_stokes;
    int cur_stokes; // current stokes index
    int cur_z;      // current channel
    int stokes_i;   // stokes type "I" index
    int stokes_q;   // stokes type "Q" index
    int stokes_u;   // stokes type "U" index
    int stokes_v;   // stokes type "V" index
    double _beam_area;
    bool cube_image_cache; // if true, cache the whole cube image. Otherwise, only cache a channel image

    // Current channel and stokes image cache
    std::unique_ptr<float[]> channel_image_data;

    // Map of cube image cache, key is the stokes index
    std::unordered_map<int, std::unique_ptr<float[]>> cube_image_data;

    // Map of computed stokes channel image cache, key is the computed stokes index
    std::unordered_map<int, std::unique_ptr<float[]>> computed_stokes_channel_image_data;

    // Current channel of computed stokes image cache
    int computed_stokes_channel;

    ImageCache();

    float CubeImageSize() const;      // MB
    float UsedReservedMemory() const; // MB

    float GetValue(int x, int y, int z, int stokes);
    float* GetImageCacheData(int z, int stokes);
    bool GetPointSpectralData(std::vector<float>& profile, int stokes, PointXy point);
    bool GetRegionSpectralData(const AxisRange& z_range, int stokes, const casacore::ArrayLattice<casacore::Bool>& mask,
        const casacore::IPosition& origin, std::map<CARTA::StatsType, std::vector<double>>& profiles);
};

} // namespace carta

#endif // CARTA_BACKEND__FRAME_IMAGECACHE_H_
