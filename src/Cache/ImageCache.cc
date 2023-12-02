/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ImageCache.h"

#include "ChannelImageCache.h"
#include "CubeImageCache.h"
#include "Logger/Logger.h"
#include "Util/Stokes.h"
#include "Util/System.h"

float FULL_IMAGE_CACHE_SIZE_AVAILABLE = 0; // MB
std::mutex FULL_IMAGE_CACHE_SIZE_AVAILABLE_MUTEX;

namespace carta {

std::unique_ptr<ImageCache> ImageCache::GetImageCache(std::shared_ptr<LoaderHelper> loader_helper) {
    if (!loader_helper->TileCacheAvailable()) {
        auto image_memory_size = ImageCache::ImageMemorySize(
            loader_helper->Width(), loader_helper->Height(), loader_helper->Depth(), loader_helper->NumStokes());
        if (FULL_IMAGE_CACHE_SIZE_AVAILABLE >= image_memory_size) {
            return std::make_unique<CubeImageCache>(loader_helper);
        }

        if (FULL_IMAGE_CACHE_SIZE_AVAILABLE > 0) {
            spdlog::info("Image too large ({:.0f} MB). Not cache the whole image data.", image_memory_size);
        }
    }
    return std::make_unique<ChannelImageCache>(loader_helper);
}

ImageCache::ImageCache(ImageCacheType type, std::shared_ptr<LoaderHelper> loader_helper)
    : _type(type), _loader_helper(loader_helper), _stokes_i(-1), _stokes_q(-1), _stokes_u(-1), _stokes_v(-1), _beam_area(DOUBLE_NAN) {
    // Get image size
    _width = _loader_helper->Width();
    _height = _loader_helper->Height();
    _depth = _loader_helper->Depth();
    _num_stokes = _loader_helper->NumStokes();

    // Get stokes type indices
    bool mute_err_msg(true);
    _loader_helper->GetStokesTypeIndex("I", _stokes_i, mute_err_msg);
    _loader_helper->GetStokesTypeIndex("Q", _stokes_q, mute_err_msg);
    _loader_helper->GetStokesTypeIndex("U", _stokes_u, mute_err_msg);
    _loader_helper->GetStokesTypeIndex("V", _stokes_v, mute_err_msg);

    // Get beam area
    _beam_area = _loader_helper->GetBeamArea();
}

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
