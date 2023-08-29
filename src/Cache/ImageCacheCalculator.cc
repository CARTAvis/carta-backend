/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ImageCacheCalculator.h"

#include "Util/Stokes.h"

namespace carta {

float ImageCacheCalculator::GetValue(CubeImageCache& cube_image_cache, int x, int y, int z, int stokes, size_t width, size_t height) {
    auto& stokes_data = cube_image_cache.stokes_data;
    size_t idx = width * height * z + width * y + x;

    if (IsComputedStokes(stokes)) {
        auto stokes_type = StokesTypes[stokes];
        int& stokes_i = cube_image_cache.stokes_i;
        int& stokes_q = cube_image_cache.stokes_q;
        int& stokes_u = cube_image_cache.stokes_u;
        int& stokes_v = cube_image_cache.stokes_v;

        if (stokes_type == CARTA::PolarizationType::Ptotal) {
            if (stokes_q > -1 && stokes_u > -1 && stokes_v > -1) {
                return CalcPtotal(stokes_data[stokes_q][idx], stokes_data[stokes_u][idx], stokes_data[stokes_v][idx]);
            }
        } else if (stokes_type == CARTA::PolarizationType::Plinear) {
            if (stokes_q > -1 && stokes_u > -1) {
                return CalcPlinear(stokes_data[stokes_q][idx], stokes_data[stokes_u][idx]);
            }
        } else if (stokes_type == CARTA::PolarizationType::PFtotal) {
            if (stokes_i > -1 && stokes_q > -1 && stokes_u > -1 && stokes_v > -1) {
                return CalcPFtotal(
                    stokes_data[stokes_i][idx], stokes_data[stokes_q][idx], stokes_data[stokes_u][idx], stokes_data[stokes_v][idx]);
            }
        } else if (stokes_type == CARTA::PolarizationType::PFlinear) {
            if (stokes_i > -1 && stokes_q > -1 && stokes_u > -1) {
                return CalcPFlinear(stokes_data[stokes_i][idx], stokes_data[stokes_q][idx], stokes_data[stokes_u][idx]);
            }
        } else if (stokes_type == CARTA::PolarizationType::Pangle) {
            if (stokes_q > -1 && stokes_u > -1) {
                return CalcPangle(stokes_data[stokes_q][idx], stokes_data[stokes_u][idx]);
            }
        }
        return FLOAT_NAN;
    }

    return stokes_data[stokes][idx];
}

bool ImageCacheCalculator::GetPointSpectralData(
    CubeImageCache& cube_image_cache, std::vector<float>& profile, int stokes, PointXy point, size_t width, size_t height, size_t depth) {
    int x, y;
    point.ToIndex(x, y);
    if (cube_image_cache.stokes_data.count(stokes) || IsComputedStokes(stokes)) {
        profile.resize(depth);
#pragma omp parallel for
        for (int z = 0; z < depth; ++z) {
            profile[z] = GetValue(cube_image_cache, x, y, z, stokes, width, height);
        }
        return true;
    }
    return false;
}

bool ImageCacheCalculator::GetRegionSpectralData(CubeImageCache& cube_image_cache, const AxisRange& z_range, int stokes, size_t width,
    size_t height, const casacore::ArrayLattice<casacore::Bool>& mask, const casacore::IPosition& origin,
    std::map<CARTA::StatsType, std::vector<double>>& profiles) {
    if (!mask.shape().empty() && (cube_image_cache.stokes_data.count(stokes) || IsComputedStokes(stokes))) {
        int x_min = origin(0);
        int y_min = origin(1);
        casacore::IPosition mask_shape(mask.shape());
        int mask_width = mask_shape(0);
        int mask_height = mask_shape(1);
        int start = z_range.from;
        int end = z_range.to;
        size_t z_size = end - start + 1;
        bool has_flux = !std::isnan(cube_image_cache.beam_area);

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
                    auto val = GetValue(cube_image_cache, x, y, z, stokes, width, height);
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
                    profiles[CARTA::StatsType::FluxDensity][idx] = sum / cube_image_cache.beam_area;
                }
            }
        }
        return true;
    }
    return false;
}

} // namespace carta
