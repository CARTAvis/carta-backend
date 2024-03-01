/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2024 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//
// Created by angus on 2019/09/23.
//

#ifndef CARTA_SRC_DATASTREAM_SMOOTHING_H_
#define CARTA_SRC_DATASTREAM_SMOOTHING_H_

#include <cstdint>
#include <limits>
#include <vector>

#ifdef _ARM_ARCH_
#include <sse2neon/sse2neon.h>
#else
#include <x86intrin.h>
#endif

#define SMOOTHING_TEMP_BUFFER_SIZE_MB 200

namespace carta {

#ifdef __AVX__
#define SIMD_WIDTH 8

static inline __m256 IsInfinity(__m256 x) {
    const __m256 sign_mask = _mm256_set1_ps(-0.0);
    const __m256 inf = _mm256_set1_ps(std::numeric_limits<float>::infinity());
    x = _mm256_andnot_ps(sign_mask, x);
    x = _mm256_cmp_ps(x, inf, _CMP_EQ_OQ);
    return x;
}

static inline float _mm256_reduce_add_ps(__m256 x) {
    __m256 t1 = _mm256_hadd_ps(x, x);
    __m256 t2 = _mm256_hadd_ps(t1, t1);
    __m128 t3 = _mm256_extractf128_ps(t2, 1);
    __m128 t4 = _mm_add_ss(_mm256_castps256_ps128(t2), t3);
    return _mm_cvtss_f32(t4);
}
#else
#define SIMD_WIDTH 4
#endif

static inline __m128 IsInfinity(__m128 x) {
    const __m128 sign_mask = _mm_set_ps1(-0.0f);
    const __m128 inf = _mm_set_ps1(std::numeric_limits<float>::infinity());

    x = _mm_andnot_ps(sign_mask, x);
    x = _mm_cmpeq_ps(x, inf);
    return x;
}

void MakeKernel(std::vector<float>& kernel, double sigma);
bool RunKernel(const std::vector<float>& kernel, const float* src_data, float* dest_data, int64_t src_width, int64_t src_height,
    int64_t dest_width, int64_t dest_height, bool vertical);
bool GaussianSmooth(const float* src_data, float* dest_data, int64_t src_width, int64_t src_height, int64_t dest_width, int64_t dest_height,
    int smoothing_factor);
bool BlockSmooth(const float* src_data, float* dest_data, int64_t src_width, int64_t src_height, int64_t dest_width, int64_t dest_height,
    int64_t x_offset, int64_t y_offset, int smoothing_factor);
bool BlockSmoothScalar(const float* src_data, float* dest_data, int64_t src_width, int64_t src_height, int64_t dest_width,
    int64_t dest_height, int64_t x_offset, int64_t y_offset, int smoothing_factor);
bool BlockSmoothSSE(const float* src_data, float* dest_data, int64_t src_width, int64_t src_height, int64_t dest_width, int64_t dest_height,
    int64_t x_offset, int64_t y_offset, int smoothing_factor);
#ifdef __AVX__
bool BlockSmoothAVX(const float* src_data, float* dest_data, int64_t src_width, int64_t src_height, int64_t dest_width, int64_t dest_height,
    int64_t x_offset, int64_t y_offset, int smoothing_factor);
#endif

void NearestNeighbor(const float* src_data, float* dest_data, int64_t src_width, int64_t dest_width, int64_t dest_height, int64_t x_offset,
    int64_t y_offset, int smoothing_factor);

} // namespace carta

#endif // CARTA_SRC_DATASTREAM_SMOOTHING_H_
