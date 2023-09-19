/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ImageCache.h"

#include "Logger/Logger.h"
#include "Util/Stokes.h"

namespace carta {

ImageCache::ImageCache(ImageCacheType type)
    : _type(type), _stokes_i(-1), _stokes_q(-1), _stokes_u(-1), _stokes_v(-1), _beam_area(DOUBLE_NAN) {}

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

void ImageCache::LoadCachedPointSpatialData(
    std::vector<float>& profile, char config, PointXy point, size_t start, size_t end, int z, int stokes, size_t width, size_t height) {
    profile.reserve(end - start);
    if (config == 'x') {
        for (unsigned int i = start; i < end; ++i) {
            profile.push_back(GetValue(i, point.y, z, stokes, width, height));
        }
    } else if (config == 'y') {
        for (unsigned int i = start; i < end; ++i) {
            profile.push_back(GetValue(point.x, i, z, stokes, width, height));
        }
    } else {
        spdlog::error("Unknown point spatial profile config: {}", config);
    }
}

bool ImageCache::DataExist(int stokes) const {
    return false;
}

void ImageCache::ValidateChannelImageCache() {}

void ImageCache::InvalidateChannelImageCache() {}

} // namespace carta
