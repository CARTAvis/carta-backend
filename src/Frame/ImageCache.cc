/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ImageCache.h"

#include "Logger/Logger.h"
#include "Util/Stokes.h"

#include <casacore/casa/BasicSL/Constants.h>

namespace carta {

ImageCache::ImageCache()
    : _width(-1),
      _height(-1),
      _depth(-1),
      _num_stokes(-1),
      _stokes_index(DEFAULT_STOKES),
      _z_index(0),
      _stokes_i(-1),
      _stokes_q(-1),
      _stokes_u(-1),
      _stokes_v(-1),
      _cube_image_cache(false) {}

std::unique_ptr<float[]>& ImageCache::GetData(int stokes) {
    return _data[stokes];
}

bool ImageCache::IsDataAvailable(int key) const {
    return _data.count(key);
}

int ImageCache::Size() const {
    return _data.size();
}

float ImageCache::CubeImageSize() const {
    return (float)(_width * _height * _depth * _num_stokes * sizeof(float)) / 1.0e6; // MB
}

float ImageCache::UsedReservedMemory() const {
    return _cube_image_cache ? CubeImageSize() : 0.0;
}

size_t ImageCache::StartIndex(int z_index, int stokes_index) const {
    if (stokes_index == CURRENT_STOKES) {
        stokes_index = _stokes_index;
    }
    if (z_index == CURRENT_Z) {
        z_index = _z_index;
    }
    if (_cube_image_cache && !IsComputedStokes(stokes_index)) {
        return _width * _height * (size_t)z_index;
    }
    return 0;
}

int ImageCache::Key(int stokes_index) const {
    if (stokes_index == CURRENT_STOKES) {
        stokes_index = _stokes_index;
    }
    // Only return non-computed stokes index, since we only cache cube image data for existing stokes types from the file
    if (_cube_image_cache && !IsComputedStokes(stokes_index)) {
        return stokes_index;
    }
    return CURRENT_CHANNEL_STOKES;
}

float ImageCache::GetValue(size_t index, int stokes) {
    if (_cube_image_cache && IsComputedStokes(stokes)) {
        auto stokes_type = StokesTypes[stokes];
        if (stokes_type == CARTA::PolarizationType::Ptotal) {
            if (_stokes_q > -1 && _stokes_u > -1 && _stokes_v > -1) {
                return CalcPtotal(_data[_stokes_q][index], _data[_stokes_u][index], _data[_stokes_v][index]);
            }
        } else if (stokes_type == CARTA::PolarizationType::Plinear) {
            if (_stokes_q > -1 && _stokes_u > -1) {
                return CalcPlinear(_data[_stokes_q][index], _data[_stokes_u][index]);
            }
        } else if (stokes_type == CARTA::PolarizationType::PFtotal) {
            if (_stokes_i > -1 && _stokes_q > -1 && _stokes_u > -1 && _stokes_v > -1) {
                return CalcPFtotal(_data[_stokes_i][index], _data[_stokes_q][index], _data[_stokes_u][index], _data[_stokes_v][index]);
            }
        } else if (stokes_type == CARTA::PolarizationType::PFlinear) {
            if (_stokes_i > -1 && _stokes_q > -1 && _stokes_u > -1) {
                return CalcPFlinear(_data[_stokes_i][index], _data[_stokes_q][index], _data[_stokes_u][index]);
            }
        } else if (stokes_type == CARTA::PolarizationType::Pangle) {
            if (_stokes_q > -1 && _stokes_u > -1) {
                return CalcPangle(_data[_stokes_q][index], _data[_stokes_u][index]);
            }
        }
        return FLOAT_NAN;
    }
    return _data[Key(stokes)][index];
}

bool ImageCache::GetPointSpectralData(std::vector<float>& profile, int stokes, PointXy point) {
    if (_cube_image_cache) {
        // A lock for cube image cache is not required here, since this process is already locked via the spectral profile mutex
        int x, y;
        point.ToIndex(x, y);
        if (IsDataAvailable(stokes) || IsComputedStokes(stokes)) {
            profile.resize(_depth);
#pragma omp parallel for
            for (int z = 0; z < _depth; ++z) {
                size_t idx = (z * _width * _height) + (_width * y + x);
                profile[z] = GetValue(idx, stokes);
            }
            return true;
        }
        spdlog::error("Invalid cube image cache for the cursor/point region spectral profile!");
    }
    return false;
}

float* ImageCache::GetImageCacheData(int z, int stokes) {
    if (_cube_image_cache && IsComputedStokes(stokes)) {
        _data[CURRENT_CHANNEL_STOKES] = std::make_unique<float[]>(_width * _height);

        auto stokes_type = StokesTypes[stokes];
        if (stokes_type == CARTA::PolarizationType::Ptotal) {
            if (_stokes_q > -1 && _stokes_u > -1 && _stokes_v > -1) {
#pragma omp parallel for
                for (int i = 0; i < _width * _height; ++i) {
                    size_t idx = StartIndex(z, _stokes_q) + i;
                    _data[CURRENT_CHANNEL_STOKES][i] = CalcPtotal(_data[_stokes_q][idx], _data[_stokes_u][idx], _data[_stokes_v][idx]);
                }
            }
        } else if (stokes_type == CARTA::PolarizationType::Plinear) {
            if (_stokes_q > -1 && _stokes_u > -1) {
#pragma omp parallel for
                for (int i = 0; i < _width * _height; ++i) {
                    size_t idx = StartIndex(z, _stokes_q) + i;
                    _data[CURRENT_CHANNEL_STOKES][i] = CalcPlinear(_data[_stokes_q][idx], _data[_stokes_u][idx]);
                }
            }
        } else if (stokes_type == CARTA::PolarizationType::PFtotal) {
            if (_stokes_i > -1 && _stokes_q > -1 && _stokes_u > -1 && _stokes_v > -1) {
#pragma omp parallel for
                for (int i = 0; i < _width * _height; ++i) {
                    size_t idx = StartIndex(z, _stokes_i) + i;
                    _data[CURRENT_CHANNEL_STOKES][i] =
                        CalcPFtotal(_data[_stokes_i][idx], _data[_stokes_q][idx], _data[_stokes_u][idx], _data[_stokes_v][idx]);
                }
            }
        } else if (stokes_type == CARTA::PolarizationType::PFlinear) {
            if (_stokes_i > -1 && _stokes_q > -1 && _stokes_u > -1) {
#pragma omp parallel for
                for (int i = 0; i < _width * _height; ++i) {
                    size_t idx = StartIndex(z, _stokes_i) + i;
                    _data[CURRENT_CHANNEL_STOKES][i] = CalcPFlinear(_data[_stokes_i][idx], _data[_stokes_q][idx], _data[_stokes_u][idx]);
                }
            }
        } else if (stokes_type == CARTA::PolarizationType::Pangle) {
            if (_stokes_q > -1 && _stokes_u > -1) {
#pragma omp parallel for
                for (int i = 0; i < _width * _height; ++i) {
                    size_t idx = StartIndex(z, _stokes_q) + i;
                    _data[CURRENT_CHANNEL_STOKES][i] = CalcPangle(_data[_stokes_q][idx], _data[_stokes_u][idx]);
                }
            }
        }
        return _data[CURRENT_CHANNEL_STOKES].get();
    }
    return _data[Key(stokes)].get() + StartIndex(z, stokes);
}

bool ImageCache::GetRegionSpectralData(const AxisRange& z_range, int stokes, const casacore::ArrayLattice<casacore::Bool>& mask,
    const casacore::IPosition& origin, std::map<CARTA::StatsType, std::vector<double>>& profiles) {
    if (!mask.shape().empty() && _cube_image_cache) {
        // A lock for cube image cache is not required here, since this process is already locked via the spectral profile mutex
        int x_min = origin(0);
        int y_min = origin(1);
        casacore::IPosition mask_shape(mask.shape());
        int width = mask_shape(0);
        int height = mask_shape(1);
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

            for (int x = x_min; x < x_min + width; ++x) {
                for (int y = y_min; y < y_min + height; ++y) {
                    size_t idx = (z * _width * _height) + (_width * y + x);
                    double val = GetValue(idx, stokes);
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

} // namespace carta
