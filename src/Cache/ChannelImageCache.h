/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__FRAME_CHANNELIMAGECACHE_H_
#define CARTA_BACKEND__FRAME_CHANNELIMAGECACHE_H_

#include "ImageCache.h"

#include "Util/Image.h"

#include <casacore/casa/Arrays/IPosition.h>
#include <casacore/lattices/Lattices/ArrayLattice.h>

#include <memory>
#include <unordered_map>
#include <vector>

namespace carta {

class ChannelImageCache : public ImageCache {
public:
    ChannelImageCache(size_t width, size_t height, size_t depth);

    float* AllocateData(int stokes, size_t data_size) override;
    float* GetChannelImageCache(int z, int stokes) override;
    inline float GetValue(int x, int y, int z, int stokes) override;

    bool LoadCachedPointSpectralData(std::vector<float>& profile, int stokes, PointXy point) override;
    bool LoadCachedRegionSpectralData(const AxisRange& z_range, int stokes, const casacore::ArrayLattice<casacore::Bool>& mask,
        const casacore::IPosition& origin, std::map<CARTA::StatsType, std::vector<double>>& profiles) override;
    bool DataExist(int stokes) const override;
    bool ChannelImageCacheValid() const override;

    void ValidateChannelImageCache() override;
    void InvalidateChannelImageCache() override;

private:
    std::unique_ptr<float[]> _channel_data;

    bool _channel_image_cache_valid; // Cached image data is valid for current z and stokes
};

} // namespace carta

#endif // CARTA_BACKEND__FRAME_CHANNELIMAGECACHE_H_
