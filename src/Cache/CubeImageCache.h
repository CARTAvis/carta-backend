/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__FRAME_CUBEIMAGECACHE_H_
#define CARTA_BACKEND__FRAME_CUBEIMAGECACHE_H_

#include "Util/Image.h"

#include <casacore/casa/Arrays/IPosition.h>
#include <casacore/lattices/Lattices/ArrayLattice.h>

#include <memory>
#include <unordered_map>
#include <vector>

namespace carta {

class CubeImageCache {
public:
    CubeImageCache();

    float* AllocateData(int stokes, size_t data_size);
    float* GetChannelImageCache(int z, int stokes, size_t width, size_t height);

    bool LoadCachedPointSpectralData(std::vector<float>& profile, int stokes, PointXy point, size_t width, size_t height, size_t depth);
    bool LoadCachedRegionSpectralData(const AxisRange& z_range, int stokes, size_t width, size_t height,
        const casacore::ArrayLattice<casacore::Bool>& mask, const casacore::IPosition& origin,
        std::map<CARTA::StatsType, std::vector<double>>& profiles);
    float GetValue(int x, int y, int z, int stokes, size_t width, size_t height);

    int& StokesI() {
        return _stokes_i;
    }
    int& StokesQ() {
        return _stokes_q;
    }
    int& StokesU() {
        return _stokes_u;
    }
    int& StokesV() {
        return _stokes_v;
    }
    double& BeamArea() {
        return _beam_area;
    }

    bool DataExist() const {
        return !_stokes_data.empty();
    }
    bool DataExist(int stokes) const {
        return _stokes_data.count(stokes);
    }

private:
    int _stokes_i; // stokes type "I" index
    int _stokes_q; // stokes type "Q" index
    int _stokes_u; // stokes type "U" index
    int _stokes_v; // stokes type "V" index
    double _beam_area;

    // Current channel of computed stokes image cache
    int _computed_stokes_channel;

    // Map of cube image cache, key is the stokes index
    std::unordered_map<int, std::unique_ptr<float[]>> _stokes_data;

    // Map of computed stokes *channel* image cache, key is the computed stokes index
    std::unordered_map<int, std::unique_ptr<float[]>> _computed_stokes_channel_data;
};

} // namespace carta

#endif // CARTA_BACKEND__FRAME_CUBEIMAGECACHE_H_
