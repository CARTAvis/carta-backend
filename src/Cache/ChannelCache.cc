/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ChannelCache.h"

#include "Frame/Frame.h"
#include "Logger/Logger.h"
#include "Timer/Timer.h"
#include "Util/Stokes.h"

namespace carta {

ChannelCache::ChannelCache(Frame* frame, std::shared_ptr<FileLoader> loader, std::mutex& image_mutex)
    : ImageCache(frame, loader, image_mutex), _channel_data(nullptr), _channel_image_cache_valid(false) {
    spdlog::info("Cache single channel image data.");
}

bool ChannelCache::FillChannelCache(std::unique_ptr<float[]>& channel_data, int z, int stokes) {
    StokesSlicer stokes_slicer = _frame->GetImageSlicer(AxisRange(ALL_X), AxisRange(ALL_Y), AxisRange(z), stokes);
    auto data_size = stokes_slicer.slicer.length().product();
    channel_data = std::make_unique<float[]>(data_size);
    if (!GetSlicerData(stokes_slicer, channel_data.get())) {
        spdlog::error("Loading channel image failed (z: {}, stokes: {})", z, stokes);
        return false;
    }
    return true;
}

float* ChannelCache::GetChannelData(int z, int stokes) {
    bool write_lock(false);
    queuing_rw_mutex_scoped cache_lock(&_cache_mutex, write_lock);
    return ChannelDataAvailable(z, stokes) ? _channel_data.get() : nullptr;
}

bool ChannelCache::LoadPointSpectralData(std::vector<float>& profile, int stokes, PointXy point) {
    return false;
}

bool ChannelCache::LoadRegionSpectralData(const AxisRange& z_range, int stokes, const casacore::ArrayLattice<casacore::Bool>& mask,
    const casacore::IPosition& origin, std::map<CARTA::StatsType, std::vector<double>>& profiles) {
    return false;
}

float ChannelCache::GetValue(int x, int y, int z, int stokes) {
    bool write_lock(false);
    queuing_rw_mutex_scoped cache_lock(&_cache_mutex, write_lock);
    return _channel_data[(_width * y) + x];
}

bool ChannelCache::ChannelDataAvailable(int z, int stokes) const {
    return (z == _frame->CurrentZ()) && (stokes == _frame->CurrentStokes()) && _channel_image_cache_valid;
}

bool ChannelCache::UpdateChannelCache(int z, int stokes) {
    bool write_lock(true);
    queuing_rw_mutex_scoped cache_lock(&_cache_mutex, write_lock);

    if (ChannelDataAvailable(z, stokes)) {
        return true;
    }

    Timer t;
    if (!FillChannelCache(_channel_data, z, stokes)) {
        _valid = false;
        return false;
    }
    auto dt = t.Elapsed();
    spdlog::performance(
        "Load {}x{} image to cache in {:.3f} ms at {:.3f} MPix/s", _width, _height, dt.ms(), (float)(_width * _height) / dt.us());

    _channel_image_cache_valid = true;
    return true;
}

void ChannelCache::UpdateValidity(int stokes) {
    bool write_lock(true);
    queuing_rw_mutex_scoped cache_lock(&_cache_mutex, write_lock);
    _channel_image_cache_valid = false;
}

} // namespace carta
