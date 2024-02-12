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

ChannelImageCache::ChannelImageCache(std::shared_ptr<FileLoader> loader, std::shared_ptr<ImageState> image_state, std::mutex& image_mutex)
    : ImageCache(loader, image_state, image_mutex), _channel_data(nullptr), _channel_image_cache_valid(false) {
    spdlog::info("Cache single channel image data.");
}

bool ChannelImageCache::FillChannelImageCache(std::unique_ptr<float[]>& channel_data, int z, int stokes) {
    StokesSlicer stokes_slicer = GetImageSlicer(AxisRange(ALL_X), AxisRange(ALL_Y), AxisRange(z), stokes);
    auto data_size = stokes_slicer.slicer.length().product();
    channel_data = std::make_unique<float[]>(data_size);
    if (!GetSlicerData(stokes_slicer, channel_data.get())) {
        spdlog::error("Loading channel image failed (z: {}, stokes: {})", z, stokes);
        return false;
    }
    return true;
}

float* ChannelImageCache::GetChannelData(int z, int stokes) {
    return CachedChannelDataAvailable(z, stokes) ? _channel_data.get() : nullptr;
}

bool ChannelImageCache::LoadCachedPointSpectralData(std::vector<float>& profile, int stokes, PointXy point) {
    return false;
}

bool ChannelImageCache::LoadCachedRegionSpectralData(const AxisRange& z_range, int stokes,
    const casacore::ArrayLattice<casacore::Bool>& mask, const casacore::IPosition& origin,
    std::map<CARTA::StatsType, std::vector<double>>& profiles) {
    return false;
}

float ChannelImageCache::GetValue(int x, int y, int z, int stokes) const {
    return CachedChannelDataAvailable(z, stokes) ? _channel_data[(_width * y) + x] : FLOAT_NAN;
}

bool ChannelImageCache::CachedChannelDataAvailable(int z, int stokes) const {
    return _image_state->IsCurrentChannel(z, stokes) && _channel_image_cache_valid;
}

bool ChannelImageCache::UpdateChannelImageCache(int z, int stokes) {
    if (CachedChannelDataAvailable(z, stokes)) {
        return true;
    }

    Timer t;
    if (!FillChannelImageCache(_channel_data, z, stokes)) {
        _valid = false;
        return false;
    }
    auto dt = t.Elapsed();
    spdlog::performance(
        "Load {}x{} image to cache in {:.3f} ms at {:.3f} MPix/s", _width, _height, dt.ms(), (float)(_width * _height) / dt.us());

    _channel_image_cache_valid = true;
    return true;
}

void ChannelImageCache::SetImageChannels(int z, int stokes) {
    _channel_image_cache_valid = false;
    _image_state->SetCurrentZ(z);
    _image_state->SetCurrentStokes(stokes);
}

} // namespace carta
