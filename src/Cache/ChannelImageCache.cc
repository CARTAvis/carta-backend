/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ChannelImageCache.h"

#include "Util/Stokes.h"

namespace carta {

ChannelImageCache::ChannelImageCache(size_t width, size_t height, size_t depth)
    : ImageCache(ImageCacheType::Channel, width, height, depth), _channel_data(nullptr), _channel_image_cache_valid(false) {}

float* ChannelImageCache::AllocateData(int stokes, size_t data_size) {
    _channel_data = std::make_unique<float[]>(data_size);
    return _channel_data.get();
}

float* ChannelImageCache::GetChannelImageCache(int z, int stokes) {
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

bool ChannelImageCache::DataExist(int stokes) const {
    return false;
}

bool ChannelImageCache::ChannelImageCacheValid() const {
    return _channel_image_cache_valid;
}

void ChannelImageCache::ValidateChannelImageCache() {
    _channel_image_cache_valid = true;
}

void ChannelImageCache::InvalidateChannelImageCache() {
    _channel_image_cache_valid = false;
}

} // namespace carta
