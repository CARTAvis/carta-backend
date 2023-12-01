/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_CACHE_CUBEIMAGECACHE_H_
#define CARTA_SRC_CACHE_CUBEIMAGECACHE_H_

#include "ImageCache.h"

#include "Util/Image.h"

#include <casacore/casa/Arrays/IPosition.h>
#include <casacore/lattices/Lattices/ArrayLattice.h>

#include <memory>
#include <unordered_map>
#include <vector>

namespace carta {

class CubeImageCache : public ImageCache {
public:
    CubeImageCache(size_t width, size_t height, size_t depth, size_t num_stokes);
    ~CubeImageCache() override;

    float* AllocateData(int stokes, size_t data_size) override;
    float* GetChannelData(int z, int stokes) override;
    inline float GetValue(int x, int y, int z, int stokes) override;

    bool LoadCachedPointSpectralData(std::vector<float>& profile, int stokes, PointXy point) override;
    bool LoadCachedRegionSpectralData(const AxisRange& z_range, int stokes, const casacore::ArrayLattice<casacore::Bool>& mask,
        const casacore::IPosition& origin, std::map<CARTA::StatsType, std::vector<double>>& profiles) override;
    bool DataExist(int stokes) const override;
    bool CachedChannelDataAvailable(bool current_channel) const override;

    void ValidateChannelImageCache() override;
    void InvalidateChannelImageCache() override;

private:
    // Current channel of computed stokes image cache
    int _computed_stokes_channel;

    // Map of cube image cache, key is the stokes index
    std::unordered_map<int, std::unique_ptr<float[]>> _stokes_data;

    // Map of computed stokes *channel* image cache, key is the computed stokes index
    std::unordered_map<int, std::unique_ptr<float[]>> _computed_stokes_channel_data;
};

} // namespace carta

#endif // CARTA_SRC_CACHE_CUBEIMAGECACHE_H_
