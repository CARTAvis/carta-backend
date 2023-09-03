/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ChannelImageCache.h"

#include "Util/Stokes.h"

namespace carta {

ChannelImageCache::ChannelImageCache() : ImageCache(ImageCacheType::Channel), _channel_data(nullptr) {}

float* ChannelImageCache::AllocateData(int stokes, size_t data_size) {
    _channel_data = std::make_unique<float[]>(data_size);
    return _channel_data.get();
}

float* ChannelImageCache::GetChannelImageCache(int z, int stokes, size_t width, size_t height) {
    return _channel_data.get();
}

float ChannelImageCache::GetValue(int x, int y, int z, int stokes, size_t width, size_t height) {
    return _channel_data[width * y + x];
}

} // namespace carta
