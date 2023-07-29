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

#define CURRENT_CHANNEL_STOKES -1

namespace carta {

struct ImageCache {
    size_t _width;
    size_t _height;
    size_t _depth;
    size_t _num_stokes;
    int _stokes_index;      // current stokes index
    int _z_index;           // current channel
    int _stokes_i;          // stokes type "I" index
    int _stokes_q;          // stokes type "Q" index
    int _stokes_u;          // stokes type "U" index
    int _stokes_v;          // stokes type "V" index
    bool _cube_image_cache; // if true, cache the whole cube image. Otherwise, only cache a channel image

    // Map of image caches
    // key = -1: image cache of the current channel and stokes data
    // key > -1: image cache of all channels data with respect to the stokes index, e.g., 0, 1, 2, or 3 (except for computed stokes indices)
    std::unordered_map<int, std::unique_ptr<float[]>> _data;

    ImageCache();

    std::unique_ptr<float[]>& GetData(int stokes);
    bool IsDataAvailable(int key) const;
    int Size() const;
    float CubeImageSize() const;      // MB
    float UsedReservedMemory() const; // MB
    size_t StartIndex(int z_index = CURRENT_Z, int stokes_index = CURRENT_STOKES) const;
    int Key(int stokes_index = CURRENT_STOKES) const; // Get image cache key
    float GetValue(size_t index, int stokes = CURRENT_STOKES);
    bool GetPointSpectralData(std::vector<float>& profile, int stokes, PointXy point);
    float* GetImageCacheData(int z = CURRENT_Z, int stokes = CURRENT_STOKES);
    bool GetRegionSpectralData(const AxisRange& z_range, int stokes, double beam_area, const casacore::ArrayLattice<casacore::Bool>& mask,
        const casacore::IPosition& origin, std::map<CARTA::StatsType, std::vector<double>>& profiles);
};

} // namespace carta

#endif // CARTA_BACKEND__FRAME_IMAGECACHE_H_
