/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ImageCache.h"

#include "ChannelImageCache.h"
#include "CubeImageCache.h"
#include "Frame/Frame.h"
#include "FullImageCache.h"
#include "Logger/Logger.h"
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
                return std::make_unique<CubeImageCache>(frame, loader, image_mutex);
            }
            ulock.unlock();
            spdlog::info("Cube image too large ({:.0f} MB). Not cache the whole image data.", full_image_memory_size);
        }
    }
    return std::make_unique<ChannelImageCache>(frame, loader, image_mutex);
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

StokesSlicer ImageCache::GetImageSlicer(const AxisRange& x_range, const AxisRange& y_range, const AxisRange& z_range, int stokes) {
    // Set stokes source for the image loader
    StokesSource stokes_source(stokes, z_range, x_range, y_range);

    // Slicer to apply z range and stokes to image shape
    // Start with entire image
    casacore::IPosition start(OriginalImageShape().size());
    start = 0;
    casacore::IPosition end(OriginalImageShape());
    end -= 1; // last position, not length

    // Slice x axis
    if (_frame->XAxis() >= 0) {
        int start_x(x_range.from), end_x(x_range.to);

        // Normalize x constants
        if (start_x == ALL_X) {
            start_x = 0;
        }
        if (end_x == ALL_X) {
            end_x = _frame->Width() - 1;
        }

        if (stokes_source.IsOriginalImage()) {
            start(_frame->XAxis()) = start_x;
            end(_frame->XAxis()) = end_x;
        } else { // Reset the slice cut for the computed stokes image
            start(_frame->XAxis()) = 0;
            end(_frame->XAxis()) = end_x - start_x;
        }
    }

    // Slice y axis
    if (_frame->YAxis() >= 0) {
        int start_y(y_range.from), end_y(y_range.to);

        // Normalize y constants
        if (start_y == ALL_Y) {
            start_y = 0;
        }
        if (end_y == ALL_Y) {
            end_y = _frame->Height() - 1;
        }

        if (stokes_source.IsOriginalImage()) {
            start(_frame->YAxis()) = start_y;
            end(_frame->YAxis()) = end_y;
        } else { // Reset the slice cut for the computed stokes image
            start(_frame->YAxis()) = 0;
            end(_frame->YAxis()) = end_y - start_y;
        }
    }

    // Slice z axis
    if (_frame->ZAxis() >= 0) {
        int start_z(z_range.from), end_z(z_range.to);

        // Normalize z constants
        if (start_z == ALL_Z) {
            start_z = 0;
        } else if (start_z == CURRENT_Z) {
            start_z = _frame->CurrentZ();
        }
        if (end_z == ALL_Z) {
            end_z = _frame->Depth() - 1;
        } else if (end_z == CURRENT_Z) {
            end_z = _frame->CurrentZ();
        }

        if (stokes_source.IsOriginalImage()) {
            start(_frame->ZAxis()) = start_z;
            end(_frame->ZAxis()) = end_z;
        } else { // Reset the slice cut for the computed stokes image
            start(_frame->ZAxis()) = 0;
            end(_frame->ZAxis()) = end_z - start_z;
        }
    }

    // Slice stokes axis
    if (_frame->StokesAxis() >= 0) {
        // Normalize stokes constant
        _frame->CheckCurrentStokes(stokes);

        if (stokes_source.IsOriginalImage()) {
            start(_frame->StokesAxis()) = stokes;
            end(_frame->StokesAxis()) = stokes;
        } else {
            // Reset the slice cut for the computed stokes image
            start(_frame->StokesAxis()) = 0;
            end(_frame->StokesAxis()) = 0;
        }
    }

    // slicer for image data
    casacore::Slicer section(start, end, casacore::Slicer::endIsLast);
    return StokesSlicer(stokes_source, section);
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

double ImageCache::GetBeamArea() {
    return _loader->CalculateBeamArea();
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
