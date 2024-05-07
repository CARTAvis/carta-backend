/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "StokesCache.h"

#include "Frame/Frame.h"
#include "Logger/Logger.h"
#include "Timer/Timer.h"
#include "Util/Stokes.h"

namespace carta {

StokesCache::StokesCache(Frame* frame, std::shared_ptr<FileLoader> loader, std::mutex& image_mutex)
    : ImageCache(frame, loader, image_mutex),
      _beam_area(_loader->CalculateBeamArea()),
      _stokes_data(nullptr),
      _stokes_image_cache_valid(false) {
    spdlog::info("Cache single cube image data.");
    _image_memory_size = ImageMemorySize(_width, _height, _depth, 1);

    // Update the availability of full image cache size
    _full_image_cache_size_available -= _image_memory_size;
    spdlog::info("{:.0f} MB of full image cache are available.", _full_image_cache_size_available);
}

StokesCache::~StokesCache() {
    // Update the availability of full image cache size
    std::unique_lock<std::mutex> ulock(_full_image_cache_size_available_mutex);
    _full_image_cache_size_available += _image_memory_size;
    ulock.unlock();
    spdlog::info("{:.0f} MB of full image cache are available.", _full_image_cache_size_available);
}

bool StokesCache::FillStokesCache(std::unique_ptr<float[]>& stokes_data, int stokes) {
    StokesSlicer stokes_slicer = _frame->GetImageSlicer(AxisRange(ALL_X), AxisRange(ALL_Y), AxisRange(ALL_Z), stokes);
    auto data_size = stokes_slicer.slicer.length().product();
    stokes_data = std::make_unique<float[]>(data_size);
    if (!GetSlicerData(stokes_slicer, stokes_data.get())) {
        spdlog::error("Loading cube image failed (stokes index: {}).", stokes);
        return false;
    }
    return true;
}

float* StokesCache::GetChannelData(int z, int stokes) {
    bool write_lock(false);
    queuing_rw_mutex_scoped cache_lock(&_cache_mutex, write_lock);
    return ChannelDataAvailable(ALL_Z, stokes) ? _stokes_data.get() + (_width * _height * z) : nullptr;
}

float StokesCache::DoGetValue(int x, int y, int z, int stokes) {
    bool write_lock(false);
    queuing_rw_mutex_scoped cache_lock(&_cache_mutex, write_lock);
    return ChannelDataAvailable(z, stokes) ? GetValue(x, y, z, stokes) : std::numeric_limits<float>::quiet_NaN();
}

float StokesCache::GetValue(int x, int y, int z, int stokes) {
    size_t idx = (_width * _height * z) + (_width * y) + x;
    return _stokes_data[idx];
}

bool StokesCache::LoadPointSpectralData(std::vector<float>& profile, int stokes, PointXy point) {
    bool write_lock(false);
    queuing_rw_mutex_scoped cache_lock(&_cache_mutex, write_lock);
    if (ChannelDataAvailable(ALL_Z, stokes)) {
        int x, y;
        point.ToIndex(x, y);
        profile.resize(_depth);
#pragma omp parallel for
        for (int z = 0; z < _depth; ++z) {
            profile[z] = GetValue(x, y, z, stokes);
        }
        return true;
    }
    return false;
}

bool StokesCache::LoadRegionSpectralData(const AxisRange& z_range, int stokes, const casacore::ArrayLattice<casacore::Bool>& mask,
    const casacore::IPosition& origin, std::map<CARTA::StatsType, std::vector<double>>& profiles) {
    bool write_lock(false);
    queuing_rw_mutex_scoped cache_lock(&_cache_mutex, write_lock);

    // Region spectral profile for computed stokes can not be directly calculated from its pixel values. It is calculated from the
    // combination of spectral profiles for stokes I, Q, U, or V.
    if (!mask.shape().empty() && ChannelDataAvailable(ALL_Z, stokes) && !IsComputedStokes(stokes)) {
        auto get_value = [&](size_t idx) { return _stokes_data[idx]; };
        DoStatisticsCalculations(z_range, mask, origin, _beam_area, get_value, profiles);
        return true;
    }
    return false;
}

bool StokesCache::ChannelDataAvailable(int z, int stokes) const {
    return (stokes == _frame->CurrentStokes()) && _stokes_image_cache_valid;
}

bool StokesCache::UpdateChannelCache(int z, int stokes) {
    bool write_lock(true);
    queuing_rw_mutex_scoped cache_lock(&_cache_mutex, write_lock);

    if (ChannelDataAvailable(z, stokes)) {
        return true;
    }

    Timer t;
    if (!FillStokesCache(_stokes_data, stokes)) {
        _valid = false;
        return false;
    }
    auto dt = t.Elapsed();
    spdlog::performance("Load {}x{}x{} image to cache in {:.3f} ms at {:.3f} MPix/s", _width, _height, _depth, dt.ms(),
        (float)(_width * _height * _depth) / dt.us());

    _stokes_image_cache_valid = true;
    return true;
}

void StokesCache::UpdateValidity(int stokes) {
    bool write_lock(true);
    queuing_rw_mutex_scoped cache_lock(&_cache_mutex, write_lock);
    _stokes_image_cache_valid = (stokes == _frame->CurrentStokes());
}

} // namespace carta
