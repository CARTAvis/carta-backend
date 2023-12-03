/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ChannelImageCache.h"

#include "Logger/Logger.h"
#include "Timer/Timer.h"
#include "Util/Stokes.h"

namespace carta {

ChannelImageCache::ChannelImageCache(std::shared_ptr<LoaderHelper> loader_helper)
    : ImageCache(loader_helper), _channel_data(nullptr), _channel_image_cache_valid(false) {}

float* ChannelImageCache::AllocateData(int stokes, size_t data_size) {
    _channel_data = std::make_unique<float[]>(data_size);
    return _channel_data.get();
}

float* ChannelImageCache::GetChannelData(int z, int stokes) {
    return _channel_data.get();
}

bool ChannelImageCache::LoadCachedPointSpectralData(std::vector<float>& profile, int stokes, PointXy point) {
    return false;
}

bool ChannelImageCache::LoadCachedRegionSpectralData(const AxisRange& z_range, int stokes,
    const casacore::ArrayLattice<casacore::Bool>& mask, const casacore::IPosition& origin,
    std::map<CARTA::StatsType, std::vector<double>>& profiles) {
    return false;
}

float ChannelImageCache::GetValue(int x, int y, int z, int stokes) {
    return _channel_data[(_width * y) + x];
}

bool ChannelImageCache::CachedChannelDataAvailable(bool current_channel) const {
    return current_channel && _channel_image_cache_valid;
}

bool ChannelImageCache::UpdateChannelImageCache(int z, int stokes) {
    if (CachedChannelDataAvailable(_loader_helper->IsCurrentChannel(z, stokes))) {
        return true;
    }

    Timer t;
    if (!_loader_helper->FillChannelImageCache(_channel_data, z, stokes)) {
        return false;
    }
    auto dt = t.Elapsed();
    spdlog::performance(
        "Load {}x{} image to cache in {:.3f} ms at {:.3f} MPix/s", _width, _height, dt.ms(), (float)(_width * _height) / dt.us());

    _channel_image_cache_valid = true;
    return true;
}

void ChannelImageCache::InvalidateChannelImageCache() {
    _channel_image_cache_valid = false;
}

} // namespace carta
