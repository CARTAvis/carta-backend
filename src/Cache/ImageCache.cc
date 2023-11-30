/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ImageCache.h"

#include "Logger/Logger.h"
#include "Util/Stokes.h"
#include "Util/System.h"

float FULL_IMAGE_CACHE_SIZE_AVAILABLE = 0; // MB
std::mutex FULL_IMAGE_CACHE_SIZE_AVAILABLE_MUTEX;

namespace carta {

ImageCache::ImageCache(ImageCacheType type, size_t width, size_t height, size_t depth, size_t num_stokes)
    : _type(type),
      _width(width),
      _height(height),
      _depth(depth),
      _num_stokes(num_stokes),
      _stokes_i(-1),
      _stokes_q(-1),
      _stokes_u(-1),
      _stokes_v(-1),
      _beam_area(DOUBLE_NAN) {}

void ImageCache::LoadCachedPointSpatialData(
    std::vector<float>& profile, char config, PointXy point, size_t start, size_t end, int z, int stokes) {
    profile.reserve(end - start);
    if (config == 'x') {
        for (unsigned int i = start; i < end; ++i) {
            profile.push_back(GetValue(i, point.y, z, stokes));
        }
    } else if (config == 'y') {
        for (unsigned int i = start; i < end; ++i) {
            profile.push_back(GetValue(point.x, i, z, stokes));
        }
    } else {
        spdlog::error("Unknown point spatial profile config: {}", config);
    }
}

void ImageCache::AssignFullImageCacheSizeAvailable(int& full_image_cache_size_available, std::string& msg) {
    if (full_image_cache_size_available > 0) {
        // Check if required full image cache hits the upper limit based on the total system memory
        int memory_upper_limit = GetTotalSystemMemory() * 9 / 10;
        if (full_image_cache_size_available > memory_upper_limit) {
            spdlog::warn("Full image cache {} MB is greater than the system upper limit {} MB, reset it to {} MB.",
                full_image_cache_size_available, memory_upper_limit, memory_upper_limit);
            full_image_cache_size_available = memory_upper_limit;
        }

        // Set the global variable for full image cache
        std::unique_lock<std::mutex> ulock_full_image_cache_size_available(FULL_IMAGE_CACHE_SIZE_AVAILABLE_MUTEX);
        FULL_IMAGE_CACHE_SIZE_AVAILABLE = full_image_cache_size_available;
        ulock_full_image_cache_size_available.unlock();
        msg += fmt::format(" Total amount of full image cache {} MB.", FULL_IMAGE_CACHE_SIZE_AVAILABLE);
    }
}

float ImageCache::ImageMemorySize(size_t width, size_t height, size_t depth, size_t num_stokes) {
    float image_cubes_size = width * height * depth * num_stokes * sizeof(float);

    // Conservatively estimate the number of computed stokes will be generated
    int num_computed_stokes = 0;
    if (num_stokes >= 4) {
        num_computed_stokes = 5;
    } else if (num_stokes == 3) {
        num_computed_stokes = 4;
    } else if (num_stokes == 2) {
        num_computed_stokes = 2;
    }
    return (image_cubes_size + num_computed_stokes * width * height * sizeof(float)) / 1.0e6; // MB
}

} // namespace carta