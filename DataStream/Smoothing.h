//
// Created by angus on 2019/09/23.
//

#ifndef CARTA_BACKEND__SMOOTHING_H_
#define CARTA_BACKEND__SMOOTHING_H_

#include <x86intrin.h>
#include <cstdint>
#include <limits>
#include <vector>

#define SMOOTHING_TEMP_BUFFER_SIZE_MB 200

#ifdef __AVX__
#define SIMD_WIDTH 8

static inline __m256 IsInfinity(__m256 x) {
    const __m256 sign_mask = _mm256_set1_ps(-0.0);
    const __m256 inf = _mm256_set1_ps(std::numeric_limits<float>::infinity());
    x = _mm256_andnot_ps(sign_mask, x);
    x = _mm256_cmp_ps(x, inf, _CMP_EQ_OQ);
    return x;
}
#else
#define SIMD_WIDTH 4

static inline __m128 IsInfinity(__m128 x) {
    const __m128 sign_mask = _mm_set_ps1(-0.0f);
    const __m128 inf = _mm_set_ps1(std::numeric_limits<float>::infinity());

    x = _mm_andnot_ps(sign_mask, x);
    x = _mm_cmpeq_ps(x, inf);
    return x;
}
#endif

void MakeKernel(std::vector<float>& kernel, double sigma);
bool RunKernel(const std::vector<float>& kernel, const float* src_data, float* dest_data, int64_t src_width, int64_t src_height,
    int64_t dest_width, int64_t dest_height, bool vertical);
bool GaussianSmooth(const float* src_data, float* dest_data, int64_t src_width, int64_t src_height, int64_t dest_width, int64_t dest_height,
    int smoothing_factor, bool performance_logging = false);
#endif // CARTA_BACKEND__SMOOTHING_H_
