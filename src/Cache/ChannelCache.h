/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_CACHE_CHANNELCACHE_H_
#define CARTA_SRC_CACHE_CHANNELCACHE_H_

#include "ImageCache.h"

#include "Util/Image.h"

#include <casacore/casa/Arrays/IPosition.h>
#include <casacore/lattices/Lattices/ArrayLattice.h>

#include <memory>
#include <unordered_map>
#include <vector>

namespace carta {

class ChannelCache : public ImageCache {
public:
    ChannelCache(Frame* frame, std::shared_ptr<FileLoader> loader, std::mutex& image_mutex);

    float* GetChannelData(int z, int stokes) override;
    inline float GetValue(int x, int y, int z, int stokes) override;

    bool LoadPointSpectralData(std::vector<float>& profile, int stokes, PointXy point) override;
    bool LoadRegionSpectralData(const AxisRange& z_range, int stokes, const casacore::ArrayLattice<casacore::Bool>& mask,
        const casacore::IPosition& origin, std::map<CARTA::StatsType, std::vector<double>>& profiles) override;
    bool ChannelDataAvailable(int z, int stokes) const override;

    bool UpdateChannelCache(int z, int stokes) override;
    void UpdateValidity(int stokes) override;

private:
    bool FillChannelCache(std::unique_ptr<float[]>& channel_data, int z, int stokes);

    std::unique_ptr<float[]> _channel_data;

    bool _channel_image_cache_valid; // Cached image data is valid for current z and stokes
};

} // namespace carta

#endif // CARTA_SRC_CACHE_CHANNELCACHE_H_