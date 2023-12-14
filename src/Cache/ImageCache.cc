/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ImageCache.h"

#include "ChannelImageCache.h"
#include "CubeImageCache.h"
#include "FullImageCache.h"
#include "Logger/Logger.h"
#include "Util/Stokes.h"
#include "Util/System.h"

float FULL_IMAGE_CACHE_SIZE_AVAILABLE = 0; // MB
std::mutex FULL_IMAGE_CACHE_SIZE_AVAILABLE_MUTEX;

namespace carta {

std::unique_ptr<ImageCache> ImageCache::GetImageCache(std::shared_ptr<LoaderHelper> loader_helper) {
    if (!loader_helper->TileCacheAvailable()) {
        auto width = loader_helper->Width();
        auto height = loader_helper->Height();
        auto depth = loader_helper->Depth();
        auto num_stokes = loader_helper->NumStokes();

        if (depth > 1) {
            auto full_image_memory_size = ImageCache::ImageMemorySize(width, height, depth, num_stokes);
            if (FULL_IMAGE_CACHE_SIZE_AVAILABLE >= full_image_memory_size) {
                if (num_stokes > 1) {
                    spdlog::info("Cache full cubes image data.");
                    return std::make_unique<FullImageCache>(loader_helper);
                }
                spdlog::info("Cache single cube image data.");
                return std::make_unique<CubeImageCache>(loader_helper);
            }
            spdlog::info("Cube image too large ({:.0f} MB). Not cache the whole image data.", full_image_memory_size);
        }
    }
    spdlog::info("Cache single channel image data.");
    return std::make_unique<ChannelImageCache>(loader_helper);
}

ImageCache::ImageCache(std::shared_ptr<LoaderHelper> loader_helper) : _loader_helper(loader_helper), _valid(true) {
    if (!_loader_helper->IsValid()) {
        _valid = false;
        return;
    }

    // Get image size
    _width = _loader_helper->Width();
    _height = _loader_helper->Height();
    _depth = _loader_helper->Depth();
    _num_stokes = _loader_helper->NumStokes();
}

void ImageCache::LoadCachedPointSpatialData(
    std::vector<float>& profile, char config, PointXy point, size_t start, size_t end, int z, int stokes) const {
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

bool ImageCache::IsValid() const {
    return _valid;
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
        std::unique_lock<std::mutex> ulock(FULL_IMAGE_CACHE_SIZE_AVAILABLE_MUTEX);
        FULL_IMAGE_CACHE_SIZE_AVAILABLE = full_image_cache_size_available;
        ulock.unlock();
        msg += fmt::format("Total amount of full image cache {} MB.", FULL_IMAGE_CACHE_SIZE_AVAILABLE);
    }
}

float ImageCache::ImageMemorySize(size_t width, size_t height, size_t depth, size_t num_stokes) {
    // Conservatively estimate the number of computed stokes will be generated
    int num_computed_stokes(0);
    if (num_stokes >= 4) {
        num_computed_stokes = 5;
    } else if (num_stokes == 3) {
        num_computed_stokes = 4;
    } else if (num_stokes == 2) {
        num_computed_stokes = 2;
    }
    return (width * height * depth * (num_stokes + num_computed_stokes) * sizeof(float)) / 1.0e6; // MB
}

void ImageCache::DoStatisticsCalculations(const AxisRange& z_range, const casacore::ArrayLattice<casacore::Bool>& mask,
    const casacore::IPosition& origin, double beam_area, const std::function<float(size_t idx)>& get_value,
    std::map<CARTA::StatsType, std::vector<double>>& profiles) {
    int x_min = origin(0);
    int y_min = origin(1);
    casacore::IPosition mask_shape(mask.shape());
    int mask_width = mask_shape(0);
    int mask_height = mask_shape(1);
    int start = z_range.from;
    int end = z_range.to;
    size_t z_size = end - start + 1;
    bool has_flux = !std::isnan(beam_area);

    profiles[CARTA::StatsType::Sum] = std::vector<double>(z_size, DOUBLE_NAN);
    profiles[CARTA::StatsType::FluxDensity] = std::vector<double>(z_size, DOUBLE_NAN);
    profiles[CARTA::StatsType::Mean] = std::vector<double>(z_size, DOUBLE_NAN);
    profiles[CARTA::StatsType::RMS] = std::vector<double>(z_size, DOUBLE_NAN);
    profiles[CARTA::StatsType::Sigma] = std::vector<double>(z_size, DOUBLE_NAN);
    profiles[CARTA::StatsType::SumSq] = std::vector<double>(z_size, DOUBLE_NAN);
    profiles[CARTA::StatsType::Min] = std::vector<double>(z_size, DOUBLE_NAN);
    profiles[CARTA::StatsType::Max] = std::vector<double>(z_size, DOUBLE_NAN);
    profiles[CARTA::StatsType::Extrema] = std::vector<double>(z_size, DOUBLE_NAN);
    profiles[CARTA::StatsType::NumPixels] = std::vector<double>(z_size, DOUBLE_NAN);

#pragma omp parallel for
    for (int z = start; z <= end; ++z) {
        double sum = 0;
        double mean = 0;
        double rms = 0;
        double sigma = 0;
        double sum_sq = 0;
        double min = std::numeric_limits<float>::max();
        double max = std::numeric_limits<float>::lowest();
        double extrema = 0;
        double num_pixels = 0;

        for (int x = x_min; x < x_min + mask_width; ++x) {
            for (int y = y_min; y < y_min + mask_height; ++y) {
                // Get pixel value
                size_t idx = (_width * _height * z) + (_width * y) + x;
                auto val = get_value(idx);
                if (!std::isnan(val) && mask.getAt(casacore::IPosition(2, x - x_min, y - y_min))) {
                    sum += val;
                    sum_sq += val * val;
                    min = val < min ? val : min;
                    max = val > max ? val : max;
                    num_pixels++;
                }
            }
        }

        if (num_pixels) {
            mean = sum / num_pixels;
            rms = sqrt(sum_sq / num_pixels);
            sigma = num_pixels > 1 ? sqrt((sum_sq - (sum * sum / num_pixels)) / (num_pixels - 1)) : 0;
            extrema = (abs(min) > abs(max) ? min : max);
            size_t idx = z - start;

            profiles[CARTA::StatsType::Sum][idx] = sum;
            profiles[CARTA::StatsType::Mean][idx] = mean;
            profiles[CARTA::StatsType::RMS][idx] = rms;
            profiles[CARTA::StatsType::Sigma][idx] = sigma;
            profiles[CARTA::StatsType::SumSq][idx] = sum_sq;
            profiles[CARTA::StatsType::Min][idx] = min;
            profiles[CARTA::StatsType::Max][idx] = max;
            profiles[CARTA::StatsType::Extrema][idx] = extrema;
            profiles[CARTA::StatsType::NumPixels][idx] = num_pixels;

            if (has_flux) {
                profiles[CARTA::StatsType::FluxDensity][idx] = sum / beam_area;
            }
        }
    }
}

} // namespace carta
