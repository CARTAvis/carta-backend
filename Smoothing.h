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

static inline __m256 is_infinity(__m256 x) {
    const __m256 SIGN_MASK = _mm256_set1_ps(-0.0);
    const __m256 INF = _mm256_set1_ps(std::numeric_limits<float>::infinity());
    x = _mm256_andnot_ps(SIGN_MASK, x);
    x = _mm256_cmp_ps(x, INF, _CMP_EQ_OQ);
    return x;
}
#else
#define SIMD_WIDTH 4

static inline __m128 is_infinity(__m128 x) {
    const __m128 SIGN_MASK = _mm_set_ps1(-0.0f);
    const __m128 INF = _mm_set_ps1(std::numeric_limits<float>::infinity());

    x = _mm_andnot_ps(SIGN_MASK, x);
    x = _mm_cmpeq_ps(x, INF);
    return x;
}
#endif

void MakeKernel(std::vector<float>& kernel, double sigma);
bool RunKernel(const std::vector<float>& kernel, const float* src_data, float* dest_data, int64_t src_width, int64_t src_height,
    int64_t dest_width, int64_t dest_height, bool vertical);
bool GaussianSmooth(const float* src_data, float* dest_data, int64_t src_width, int64_t src_height, int64_t dest_width, int64_t dest_height,
    int smoothing_factor);
#endif // CARTA_BACKEND__SMOOTHING_H_
