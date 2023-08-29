/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "CubeImageCache.h"

#include "Util/Stokes.h"

namespace carta {

CubeImageCache::CubeImageCache()
    : stokes_i(-1), stokes_q(-1), stokes_u(-1), stokes_v(-1), beam_area(DOUBLE_NAN), computed_stokes_channel(-1) {}

float* CubeImageCache::GetChannelImageCache(int z, int stokes, size_t width, size_t height) {
    if (IsComputedStokes(stokes)) {
        if (computed_stokes_channel_data.count(stokes) && computed_stokes_channel == z) {
            return computed_stokes_channel_data[stokes].get();
        }

        // Calculate the channel image data for computed stokes
        computed_stokes_channel_data[stokes] = std::make_unique<float[]>(width * height);
        computed_stokes_channel = z;

        auto stokes_type = StokesTypes[stokes];
        size_t start_idx = z * width * height;
        if (stokes_type == CARTA::PolarizationType::Ptotal) {
            if (stokes_q > -1 && stokes_u > -1 && stokes_v > -1) {
#pragma omp parallel for
                for (int i = 0; i < width * height; ++i) {
                    size_t idx = start_idx + i;
                    computed_stokes_channel_data[stokes][i] =
                        CalcPtotal(stokes_data[stokes_q][idx], stokes_data[stokes_u][idx], stokes_data[stokes_v][idx]);
                }
            }
        } else if (stokes_type == CARTA::PolarizationType::Plinear) {
            if (stokes_q > -1 && stokes_u > -1) {
#pragma omp parallel for
                for (int i = 0; i < width * height; ++i) {
                    size_t idx = start_idx + i;
                    computed_stokes_channel_data[stokes][i] = CalcPlinear(stokes_data[stokes_q][idx], stokes_data[stokes_u][idx]);
                }
            }
        } else if (stokes_type == CARTA::PolarizationType::PFtotal) {
            if (stokes_i > -1 && stokes_q > -1 && stokes_u > -1 && stokes_v > -1) {
#pragma omp parallel for
                for (int i = 0; i < width * height; ++i) {
                    size_t idx = start_idx + i;
                    computed_stokes_channel_data[stokes][i] = CalcPFtotal(
                        stokes_data[stokes_i][idx], stokes_data[stokes_q][idx], stokes_data[stokes_u][idx], stokes_data[stokes_v][idx]);
                }
            }
        } else if (stokes_type == CARTA::PolarizationType::PFlinear) {
            if (stokes_i > -1 && stokes_q > -1 && stokes_u > -1) {
#pragma omp parallel for
                for (int i = 0; i < width * height; ++i) {
                    size_t idx = start_idx + i;
                    computed_stokes_channel_data[stokes][i] =
                        CalcPFlinear(stokes_data[stokes_i][idx], stokes_data[stokes_q][idx], stokes_data[stokes_u][idx]);
                }
            }
        } else if (stokes_type == CARTA::PolarizationType::Pangle) {
            if (stokes_q > -1 && stokes_u > -1) {
#pragma omp parallel for
                for (int i = 0; i < width * height; ++i) {
                    size_t idx = start_idx + i;
                    computed_stokes_channel_data[stokes][i] = CalcPangle(stokes_data[stokes_q][idx], stokes_data[stokes_u][idx]);
                }
            }
        }
        return computed_stokes_channel_data[stokes].get();
    }

    return stokes_data[stokes].get() + width * height * z;
}

} // namespace carta
