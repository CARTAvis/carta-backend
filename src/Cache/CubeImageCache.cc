/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "CubeImageCache.h"

#include "Util/Stokes.h"

namespace carta {

CubeImageCache::CubeImageCache(size_t width, size_t height, size_t depth)
    : ImageCache(ImageCacheType::Cube, width, height, depth), _computed_stokes_channel(-1) {}

float* CubeImageCache::AllocateData(int stokes, size_t data_size) {
    _stokes_data[stokes] = std::make_unique<float[]>(data_size);
    return _stokes_data[stokes].get();
}

float* CubeImageCache::GetChannelImageCache(int z, int stokes) {
    if (IsComputedStokes(stokes)) {
        if (_computed_stokes_channel_data.count(stokes) && _computed_stokes_channel == z) {
            return _computed_stokes_channel_data[stokes].get();
        }

        // Calculate the channel image data for computed stokes
        _computed_stokes_channel_data[stokes] = std::make_unique<float[]>(_width * _height);
        _computed_stokes_channel = z;

        auto stokes_type = StokesTypes[stokes];
        size_t start_idx = z * _width * _height;
        if (stokes_type == CARTA::PolarizationType::Ptotal) {
            if (_stokes_q > -1 && _stokes_u > -1 && _stokes_v > -1) {
#pragma omp parallel for
                for (int i = 0; i < _width * _height; ++i) {
                    size_t idx = start_idx + i;
                    _computed_stokes_channel_data[stokes][i] =
                        CalcPtotal(_stokes_data[_stokes_q][idx], _stokes_data[_stokes_u][idx], _stokes_data[_stokes_v][idx]);
                }
            }
        } else if (stokes_type == CARTA::PolarizationType::Plinear) {
            if (_stokes_q > -1 && _stokes_u > -1) {
#pragma omp parallel for
                for (int i = 0; i < _width * _height; ++i) {
                    size_t idx = start_idx + i;
                    _computed_stokes_channel_data[stokes][i] = CalcPlinear(_stokes_data[_stokes_q][idx], _stokes_data[_stokes_u][idx]);
                }
            }
        } else if (stokes_type == CARTA::PolarizationType::PFtotal) {
            if (_stokes_i > -1 && _stokes_q > -1 && _stokes_u > -1 && _stokes_v > -1) {
#pragma omp parallel for
                for (int i = 0; i < _width * _height; ++i) {
                    size_t idx = start_idx + i;
                    _computed_stokes_channel_data[stokes][i] = CalcPFtotal(_stokes_data[_stokes_i][idx], _stokes_data[_stokes_q][idx],
                        _stokes_data[_stokes_u][idx], _stokes_data[_stokes_v][idx]);
                }
            }
        } else if (stokes_type == CARTA::PolarizationType::PFlinear) {
            if (_stokes_i > -1 && _stokes_q > -1 && _stokes_u > -1) {
#pragma omp parallel for
                for (int i = 0; i < _width * _height; ++i) {
                    size_t idx = start_idx + i;
                    _computed_stokes_channel_data[stokes][i] =
                        CalcPFlinear(_stokes_data[_stokes_i][idx], _stokes_data[_stokes_q][idx], _stokes_data[_stokes_u][idx]);
                }
            }
        } else if (stokes_type == CARTA::PolarizationType::Pangle) {
            if (_stokes_q > -1 && _stokes_u > -1) {
#pragma omp parallel for
                for (int i = 0; i < _width * _height; ++i) {
                    size_t idx = start_idx + i;
                    _computed_stokes_channel_data[stokes][i] = CalcPangle(_stokes_data[_stokes_q][idx], _stokes_data[_stokes_u][idx]);
                }
            }
        }
        return _computed_stokes_channel_data[stokes].get();
    }

    return _stokes_data[stokes].get() + _width * _height * z;
}

float CubeImageCache::GetValue(int x, int y, int z, int stokes) {
    size_t idx = _width * _height * z + _width * y + x;

    if (IsComputedStokes(stokes)) {
        auto stokes_type = StokesTypes[stokes];

        if (stokes_type == CARTA::PolarizationType::Ptotal) {
            if (_stokes_q > -1 && _stokes_u > -1 && _stokes_v > -1) {
                return CalcPtotal(_stokes_data[_stokes_q][idx], _stokes_data[_stokes_u][idx], _stokes_data[_stokes_v][idx]);
            }
        } else if (stokes_type == CARTA::PolarizationType::Plinear) {
            if (_stokes_q > -1 && _stokes_u > -1) {
                return CalcPlinear(_stokes_data[_stokes_q][idx], _stokes_data[_stokes_u][idx]);
            }
        } else if (stokes_type == CARTA::PolarizationType::PFtotal) {
            if (_stokes_i > -1 && _stokes_q > -1 && _stokes_u > -1 && _stokes_v > -1) {
                return CalcPFtotal(
                    _stokes_data[_stokes_i][idx], _stokes_data[_stokes_q][idx], _stokes_data[_stokes_u][idx], _stokes_data[_stokes_v][idx]);
            }
        } else if (stokes_type == CARTA::PolarizationType::PFlinear) {
            if (_stokes_i > -1 && _stokes_q > -1 && _stokes_u > -1) {
                return CalcPFlinear(_stokes_data[_stokes_i][idx], _stokes_data[_stokes_q][idx], _stokes_data[_stokes_u][idx]);
            }
        } else if (stokes_type == CARTA::PolarizationType::Pangle) {
            if (_stokes_q > -1 && _stokes_u > -1) {
                return CalcPangle(_stokes_data[_stokes_q][idx], _stokes_data[_stokes_u][idx]);
            }
        }
        return FLOAT_NAN;
    }

    return _stokes_data[stokes][idx];
}

bool CubeImageCache::LoadCachedPointSpectralData(std::vector<float>& profile, int stokes, PointXy point) {
    int x, y;
    point.ToIndex(x, y);
    if (_stokes_data.count(stokes) || IsComputedStokes(stokes)) {
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
    if (!mask.shape().empty() && (_stokes_data.count(stokes) || IsComputedStokes(stokes))) {
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
                    auto val = GetValue(x, y, z, stokes);
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
