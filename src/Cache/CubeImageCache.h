/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__FRAME_CUBEIMAGECACHE_H_
#define CARTA_BACKEND__FRAME_CUBEIMAGECACHE_H_

#include <memory>
#include <unordered_map>

namespace carta {

struct CubeImageCache {
    int stokes_i; // stokes type "I" index
    int stokes_q; // stokes type "Q" index
    int stokes_u; // stokes type "U" index
    int stokes_v; // stokes type "V" index
    double beam_area;

    // Current channel of computed stokes image cache
    int computed_stokes_channel;

    // Map of cube image cache, key is the stokes index
    std::unordered_map<int, std::unique_ptr<float[]>> stokes_data;

    // Map of computed stokes *channel* image cache, key is the computed stokes index
    std::unordered_map<int, std::unique_ptr<float[]>> computed_stokes_channel_data;

    CubeImageCache();

    float* GetChannelImageCache(int z, int stokes, size_t width, size_t height);
};

} // namespace carta

#endif // CARTA_BACKEND__FRAME_CUBEIMAGECACHE_H_
