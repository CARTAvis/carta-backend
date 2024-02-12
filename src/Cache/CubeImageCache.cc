/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "CubeImageCache.h"

#include "Logger/Logger.h"
#include "Timer/Timer.h"
#include "Util/Stokes.h"

namespace carta {

CubeImageCache::CubeImageCache(std::shared_ptr<LoaderHelper> loader_helper)
    : ImageCache(loader_helper), _beam_area(_loader_helper->GetBeamArea()), _stokes_data(nullptr), _stokes_image_cache_valid(false) {
    _image_memory_size = ImageMemorySize(_width, _height, _depth, 1);

    // Update the availability of full image cache size
    std::unique_lock<std::mutex> ulock(_full_image_cache_size_available_mutex);
    _full_image_cache_size_available -= _image_memory_size;
    ulock.unlock();
    spdlog::info("{:.0f} MB of full image cache are available.", _full_image_cache_size_available);
}

CubeImageCache::~CubeImageCache() {
    // Update the availability of full image cache size
    std::unique_lock<std::mutex> ulock(_full_image_cache_size_available_mutex);
    _full_image_cache_size_available += _image_memory_size;
    ulock.unlock();
    spdlog::info("{:.0f} MB of full image cache are available.", _full_image_cache_size_available);
}

float* CubeImageCache::GetChannelData(int z, int stokes) {
    return CachedChannelDataAvailable(z, stokes) ? _stokes_data.get() + (_width * _height * z) : nullptr;
}

float CubeImageCache::GetValue(int x, int y, int z, int stokes) const {
    size_t idx = (_width * _height * z) + (_width * y) + x;
    return CachedChannelDataAvailable(z, stokes) ? _stokes_data[idx] : FLOAT_NAN;
}

bool CubeImageCache::LoadCachedPointSpectralData(std::vector<float>& profile, int stokes, PointXy point) {
    if (CachedChannelDataAvailable(ALL_Z, stokes)) {
        int x, y;
        point.ToIndex(x, y);
        profile.resize(_depth);
#pragma omp parallel for
        for (int z = 0; z < _depth; ++z) {
            size_t idx = (_width * _height * z) + (_width * y) + x;
            profile[z] = _stokes_data[idx];
        }
        return true;
    }
    return false;
}

bool CubeImageCache::LoadCachedRegionSpectralData(const AxisRange& z_range, int stokes, const casacore::ArrayLattice<casacore::Bool>& mask,
    const casacore::IPosition& origin, std::map<CARTA::StatsType, std::vector<double>>& profiles) {
    // Region spectral profile for computed stokes can not be directly calculated from its pixel values. It is calculated from the
    // combination of spectral profiles for stokes I, Q, U, or V.
    if (!mask.shape().empty() && CachedChannelDataAvailable(ALL_Z, stokes) && !IsComputedStokes(stokes)) {
        auto get_value = [&](size_t idx) { return _stokes_data[idx]; };
        DoStatisticsCalculations(z_range, mask, origin, _beam_area, get_value, profiles);
        return true;
    }
    return false;
}

bool CubeImageCache::CachedChannelDataAvailable(int z, int stokes) const {
    return _loader_helper->IsCurrentStokes(stokes) && _stokes_image_cache_valid;
}

bool CubeImageCache::UpdateChannelImageCache(int z, int stokes) {
    if (CachedChannelDataAvailable(z, stokes)) {
        return true;
    }

    Timer t;
    if (!_loader_helper->FillCubeImageCache(_stokes_data, stokes)) {
        _valid = false;
        return false;
    }
    auto dt = t.Elapsed();
    spdlog::performance("Load {}x{}x{} image to cache in {:.3f} ms at {:.3f} MPix/s", _width, _height, _depth, dt.ms(),
        (float)(_width * _height * _depth) / dt.us());

    _stokes_image_cache_valid = true;
    return true;
}

void CubeImageCache::SetImageChannels(int z, int stokes) {
    if (!_loader_helper->IsCurrentStokes(stokes)) {
        _stokes_image_cache_valid = false;
    }
    _loader_helper->SetImageChannels(z, stokes);
}

} // namespace carta
