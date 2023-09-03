/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ImageCache.h"

#include "Logger/Logger.h"
#include "Util/Stokes.h"

namespace carta {

ImageCache::ImageCache(ImageCacheType type) : _type(type) {}

float* ImageCache::AllocateData(int stokes, size_t data_size) {
    return nullptr;
}

float* ImageCache::GetChannelImageCache(int z, int stokes, size_t width, size_t height) {
    return nullptr;
}

float ImageCache::GetValue(int x, int y, int z, int stokes, size_t width, size_t height) {
    return FLOAT_NAN;
}

bool ImageCache::LoadCachedPointSpectralData(
    std::vector<float>& profile, int stokes, PointXy point, size_t width, size_t height, size_t depth) {
    return false;
}

bool ImageCache::LoadCachedRegionSpectralData(const AxisRange& z_range, int stokes, size_t width, size_t height,
    const casacore::ArrayLattice<casacore::Bool>& mask, const casacore::IPosition& origin,
    std::map<CARTA::StatsType, std::vector<double>>& profiles) {
    return false;
}

bool ImageCache::DataExist() const {
    return false;
}

bool ImageCache::DataExist(int stokes) const {
    return false;
}

} // namespace carta
