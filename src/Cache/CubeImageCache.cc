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
    // Update the availability of full image cache size
    std::unique_lock<std::mutex> ulock(FULL_IMAGE_CACHE_SIZE_AVAILABLE_MUTEX);
    FULL_IMAGE_CACHE_SIZE_AVAILABLE -= ImageMemorySize(_width, _height, _depth, 1);
    ulock.unlock();
}

CubeImageCache::~CubeImageCache() {
    // Update the availability of full image cache size
    std::unique_lock<std::mutex> ulock(FULL_IMAGE_CACHE_SIZE_AVAILABLE_MUTEX);
    FULL_IMAGE_CACHE_SIZE_AVAILABLE += ImageMemorySize(_width, _height, _depth, 1);
    ulock.unlock();
}

float* CubeImageCache::GetChannelData(int z, int stokes) {
    return CachedChannelDataAvailable(z, stokes) ? _stokes_data.get() + (_width * _height * z) : nullptr;
}

float CubeImageCache::GetValue(int x, int y, int z, int stokes) const {
    size_t idx = (_width * _height * z) + (_width * y) + x;
    return CachedChannelDataAvailable(z, stokes) ? _stokes_data[idx] : FLOAT_NAN;
}

bool CubeImageCache::LoadCachedPointSpectralData(std::vector<float>& profile, int stokes, PointXy point) {
    int x, y;
    point.ToIndex(x, y);
    if (CachedChannelDataAvailable(ALL_Z, stokes)) {
        profile.resize(_depth);
#pragma omp parallel for
        for (int z = 0; z < _depth; ++z) {
            profile[z] = GetValue(x, y, z, stokes);
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
        int x_min = origin(0);
        int y_min = origin(1);
        casacore::IPosition mask_shape(mask.shape());
        int mask_width = mask_shape(0);
        int mask_height = mask_shape(1);
        int start = z_range.from;
        int end = z_range.to;
        size_t z_size = end - start + 1;
        bool has_flux = !std::isnan(_beam_area);

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
                    auto val = _stokes_data[idx];
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
                    profiles[CARTA::StatsType::FluxDensity][idx] = sum / _beam_area;
                }
            }
        }
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
