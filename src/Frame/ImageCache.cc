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
    : width(-1),
      height(-1),
      depth(-1),
      num_stokes(-1),
      stokes_index(DEFAULT_STOKES),
      z_index(0),
      stokes_i(-1),
      stokes_q(-1),
      stokes_u(-1),
      stokes_v(-1),
      cube_image_cache(false) {}

bool ImageCache::Exist(int key) const {
    return data.count(key);
}

int ImageCache::Size() const {
    return data.size();
}

float ImageCache::CubeImageSize() const {
    return (float)(width * height * depth * num_stokes * sizeof(float)) / 1.0e6; // MB
}

float ImageCache::UsedReservedMemory() const {
    return cube_image_cache ? CubeImageSize() : 0.0;
}

size_t ImageCache::StartIndex(int z_index_, int stokes_index_) const {
    if (stokes_index_ == CURRENT_STOKES) {
        stokes_index_ = stokes_index;
    }
    if (z_index_ == CURRENT_Z) {
        z_index_ = z_index;
    }
    if (cube_image_cache && !IsComputedStokes(stokes_index_)) {
        return width * height * (size_t)z_index_;
    }
    return 0;
}

int ImageCache::Key(int stokes_index_) const {
    if (stokes_index_ == CURRENT_STOKES) {
        stokes_index_ = stokes_index;
    }
    // Only return non-computed stokes index, since we only cache cube image data for existing stokes types from the file
    if (cube_image_cache && !IsComputedStokes(stokes_index_)) {
        return stokes_index_;
    }
    return CURRENT_CHANNEL_STOKES;
}

float ImageCache::GetValue(size_t index, int stokes) {
    if (cube_image_cache && IsComputedStokes(stokes)) {
        auto stokes_type = StokesTypes[stokes];
        if (stokes_type == CARTA::PolarizationType::Ptotal) {
            if (stokes_q > -1 && stokes_u > -1 && stokes_v > -1) {
                return CalcPtotal(data[stokes_q][index], data[stokes_u][index], data[stokes_v][index]);
            }
        } else if (stokes_type == CARTA::PolarizationType::Plinear) {
            if (stokes_q > -1 && stokes_u > -1) {
                return CalcPlinear(data[stokes_q][index], data[stokes_u][index]);
            }
        } else if (stokes_type == CARTA::PolarizationType::PFtotal) {
            if (stokes_i > -1 && stokes_q > -1 && stokes_u > -1 && stokes_v > -1) {
                return CalcPFtotal(data[stokes_i][index], data[stokes_q][index], data[stokes_u][index], data[stokes_v][index]);
            }
        } else if (stokes_type == CARTA::PolarizationType::PFlinear) {
            if (stokes_i > -1 && stokes_q > -1 && stokes_u > -1) {
                return CalcPFlinear(data[stokes_i][index], data[stokes_q][index], data[stokes_u][index]);
            }
        } else if (stokes_type == CARTA::PolarizationType::Pangle) {
            if (stokes_q > -1 && stokes_u > -1) {
                return CalcPangle(data[stokes_q][index], data[stokes_u][index]);
            }
        }
        return FLOAT_NAN;
    }
    return data[Key(stokes)][index];
}

bool ImageCache::GetPointSpectralData(std::vector<float>& profile, int stokes, PointXy point) {
    if (cube_image_cache) {
        // A lock for cube image cache is not required here, since this process is already locked via the spectral profile mutex
        int x, y;
        point.ToIndex(x, y);
        if (Exist(stokes) || IsComputedStokes(stokes)) {
            profile.resize(depth);
#pragma omp parallel for
            for (int z = 0; z < depth; ++z) {
                size_t idx = (z * width * height) + (width * y + x);
                profile[z] = GetValue(idx, stokes);
            }
            return true;
        }
        spdlog::error("Invalid cube image cache for the cursor/point region spectral profile!");
    }
    return false;
}

float* ImageCache::GetImageCacheData(int z, int stokes) {
    if (cube_image_cache && IsComputedStokes(stokes)) {
        data[CURRENT_CHANNEL_STOKES] = std::make_unique<float[]>(width * height);

        auto stokes_type = StokesTypes[stokes];
        if (stokes_type == CARTA::PolarizationType::Ptotal) {
            if (stokes_q > -1 && stokes_u > -1 && stokes_v > -1) {
#pragma omp parallel for
                for (int i = 0; i < width * height; ++i) {
                    size_t idx = StartIndex(z, stokes_q) + i;
                    data[CURRENT_CHANNEL_STOKES][i] = CalcPtotal(data[stokes_q][idx], data[stokes_u][idx], data[stokes_v][idx]);
                }
            }
        } else if (stokes_type == CARTA::PolarizationType::Plinear) {
            if (stokes_q > -1 && stokes_u > -1) {
#pragma omp parallel for
                for (int i = 0; i < width * height; ++i) {
                    size_t idx = StartIndex(z, stokes_q) + i;
                    data[CURRENT_CHANNEL_STOKES][i] = CalcPlinear(data[stokes_q][idx], data[stokes_u][idx]);
                }
            }
        } else if (stokes_type == CARTA::PolarizationType::PFtotal) {
            if (stokes_i > -1 && stokes_q > -1 && stokes_u > -1 && stokes_v > -1) {
#pragma omp parallel for
                for (int i = 0; i < width * height; ++i) {
                    size_t idx = StartIndex(z, stokes_i) + i;
                    data[CURRENT_CHANNEL_STOKES][i] =
                        CalcPFtotal(data[stokes_i][idx], data[stokes_q][idx], data[stokes_u][idx], data[stokes_v][idx]);
                }
            }
        } else if (stokes_type == CARTA::PolarizationType::PFlinear) {
            if (stokes_i > -1 && stokes_q > -1 && stokes_u > -1) {
#pragma omp parallel for
                for (int i = 0; i < width * height; ++i) {
                    size_t idx = StartIndex(z, stokes_i) + i;
                    data[CURRENT_CHANNEL_STOKES][i] = CalcPFlinear(data[stokes_i][idx], data[stokes_q][idx], data[stokes_u][idx]);
                }
            }
        } else if (stokes_type == CARTA::PolarizationType::Pangle) {
            if (stokes_q > -1 && stokes_u > -1) {
#pragma omp parallel for
                for (int i = 0; i < width * height; ++i) {
                    size_t idx = StartIndex(z, stokes_q) + i;
                    data[CURRENT_CHANNEL_STOKES][i] = CalcPangle(data[stokes_q][idx], data[stokes_u][idx]);
                }
            }
        }
        return data[CURRENT_CHANNEL_STOKES].get();
    }
    return data[Key(stokes)].get() + StartIndex(z, stokes);
}

bool ImageCache::GetRegionSpectralData(const AxisRange& z_range, int stokes, const casacore::ArrayLattice<casacore::Bool>& mask,
    const casacore::IPosition& origin, std::map<CARTA::StatsType, std::vector<double>>& profiles) {
    if (!mask.shape().empty() && cube_image_cache) {
        // A lock for cube image cache is not required here, since this process is already locked via the spectral profile mutex
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
                    size_t idx = (z * width * height) + (width * y + x);
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
