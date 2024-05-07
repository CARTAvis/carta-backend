/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ImageCache.h"

#include "ChannelCache.h"
#include "Frame/Frame.h"
#include "FullImageCache.h"
#include "Logger/Logger.h"
#include "StokesCache.h"
#include "Util/Stokes.h"
#include "Util/System.h"

namespace carta {

float ImageCache::_full_image_cache_size_available = 0; // MB
std::mutex ImageCache::_full_image_cache_size_available_mutex;

std::unique_ptr<ImageCache> ImageCache::GetImageCache(Frame* frame, std::shared_ptr<FileLoader> loader, std::mutex& image_mutex) {
    if (!(loader->UseTileCache() && loader->HasMip(2))) {
        auto width = frame->Width();
        auto height = frame->Height();
        auto depth = frame->Depth();
        auto num_stokes = frame->NumStokes();

        if (depth > 1) {
            auto full_image_memory_size = ImageCache::ImageMemorySize(width, height, depth, num_stokes);
            std::unique_lock<std::mutex> ulock(_full_image_cache_size_available_mutex);
            if (_full_image_cache_size_available >= full_image_memory_size) {
                if (num_stokes > 1) {
                    return std::make_unique<FullImageCache>(frame, loader, image_mutex);
                }
                return std::make_unique<StokesCache>(frame, loader, image_mutex);
            }
            ulock.unlock();
            spdlog::info("Cube image too large ({:.0f} MB). Not cache the whole image data.", full_image_memory_size);
        }
    }
    return std::make_unique<ChannelCache>(frame, loader, image_mutex);
}

ImageCache::ImageCache(Frame* frame, std::shared_ptr<FileLoader> loader, std::mutex& image_mutex)
    : _frame(frame), _loader(loader), _image_mutex(image_mutex), _valid(true), _image_memory_size(0) {
    if (!_loader || !_frame) {
        _valid = false;
        spdlog::error("Image loader helper is invalid!");
    }

    // Get image size
    _width = _frame->Width();
    _height = _frame->Height();
    _depth = _frame->Depth();
    _num_stokes = _frame->NumStokes();
}

casacore::IPosition ImageCache::OriginalImageShape() const {
    return _frame->ImageShape();
}

bool ImageCache::GetSlicerData(const StokesSlicer& stokes_slicer, float* data) {
    // Get image data with a slicer applied
    casacore::Array<float> tmp(stokes_slicer.slicer.length(), data, casacore::StorageInitPolicy::SHARE);
    std::unique_lock<std::mutex> ulock(_image_mutex);
    bool data_ok = _loader->GetSlice(tmp, stokes_slicer);
    _loader->CloseImageIfUpdated();
    ulock.unlock();
    return data_ok;
}

bool ImageCache::GetStokesTypeIndex(const string& coordinate, int& stokes_index, bool mute_err_msg) {
    // Coordinate could be profile (x, y, z), stokes string (I, Q, U), or combination (Ix, Qy)
    bool is_stokes_string = StokesStringTypes.find(coordinate) != StokesStringTypes.end();
    bool is_combination = (coordinate.size() > 1 && (coordinate.back() == 'x' || coordinate.back() == 'y' || coordinate.back() == 'z'));

    if (is_combination || is_stokes_string) {
        bool stokes_ok(false);

        std::string stokes_string;
        if (is_stokes_string) {
            stokes_string = coordinate;
        } else {
            stokes_string = coordinate.substr(0, coordinate.size() - 1);
        }

        if (StokesStringTypes.count(stokes_string)) {
            CARTA::PolarizationType stokes_type = StokesStringTypes[stokes_string];
            if (_loader->GetStokesTypeIndex(stokes_type, stokes_index)) {
                stokes_ok = true;
            } else if (IsComputedStokes(stokes_string)) {
                stokes_index = StokesStringTypes.at(stokes_string);
                stokes_ok = true;
            } else {
                int assumed_stokes_index = (StokesValues[stokes_type] - 1) % 4;
                if (_frame->NumStokes() > assumed_stokes_index) {
                    stokes_index = assumed_stokes_index;
                    stokes_ok = true;
                    spdlog::warn("Can not get stokes index from the header. Assuming stokes {} index is {}.", stokes_string, stokes_index);
                }
            }
        }
        if (!stokes_ok && !mute_err_msg) {
            spdlog::error("Spectral or spatial requirement {} failed: invalid stokes axis for image.", coordinate);
            return false;
        }
    } else {
        stokes_index = _frame->CurrentStokes(); // current stokes
    }
    return true;
}

bool ImageCache::TileCacheAvailable() {
    return _loader->UseTileCache() && _loader->HasMip(2);
}

bool ImageCache::LoadPointSpatialData(
    std::vector<float>& profile, char config, PointXy point, size_t start, size_t end, int z, int stokes) {
    bool write_lock(false);
    queuing_rw_mutex_scoped cache_lock(&_cache_mutex, write_lock);
    if (ChannelDataAvailable(z, stokes)) {
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
        return true;
    }
    return false;
}

bool ImageCache::IsValid() const {
    return _valid;
}

void ImageCache::SetFullImageCacheSize(int& full_image_cache_size_available, std::string& msg) {
    if (full_image_cache_size_available > 0) {
        // Check if required full image cache hits the upper limit based on the total system memory
        int memory_upper_limit = GetTotalSystemMemory() * 9 / 10;
        if (full_image_cache_size_available > memory_upper_limit) {
            spdlog::warn("Full image cache {} MB is greater than the system upper limit {} MB, reset it to {} MB.",
                full_image_cache_size_available, memory_upper_limit, memory_upper_limit);
            full_image_cache_size_available = memory_upper_limit;
        }
    } else if (full_image_cache_size_available < 0) {
        full_image_cache_size_available = 0;
    }

    // Set the global variable for full image cache
    std::unique_lock<std::mutex> ulock(_full_image_cache_size_available_mutex);
    _full_image_cache_size_available = full_image_cache_size_available;
    ulock.unlock();
    msg += fmt::format("Total amount of full image cache {} MB.", _full_image_cache_size_available);
}

float ImageCache::ImageMemorySize(size_t width, size_t height, size_t depth, size_t num_stokes) {
    return (float)(width * height * depth * num_stokes) * sizeof(float) / 1.0e6; // MB
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
