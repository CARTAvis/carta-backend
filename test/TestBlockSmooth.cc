/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <vector>
#include <random>

#include <fmt/format.h>
#include <gtest/gtest.h>
#include <casa/Arrays/Matrix.h>
#include <casa/Arrays/ArrayMath.h>

#include "../src/DataStream/Smoothing.h"
#include "../src/Timer/Timer.h"

#define MAX_ABS_ERROR 1.0e-3f
#define MAX_SUM_ERROR 1.0e-1f

// Minimum speedup of 10% expected
#define MINIMUM_SPEEDUP 1.1
#define NUM_ITERS 100
#define MAX_DOWNSAMPLE_FACTOR 256

typedef casacore::Matrix<float> Matrix2F;

using namespace std;

random_device rd;
mt19937 mt(rd());
uniform_real_distribution<float> float_random(0, 1.0f);
// Random image widths and heights in range [512, 1024]
uniform_int_distribution<int> size_random(512, 1024);


Matrix2F RandomMatrix(size_t rows, size_t columns) {
    Matrix2F m(rows, columns);

    for (auto i = 0; i < m.nrow(); i++) {
        for (auto j = 0; j < m.ncolumn(); j++) {
            m(i, j) = float_random(mt);
        }
    }
    return std::move(m);
}

Matrix2F DownsampleTileScalar(const Matrix2F& m, int downsample_factor) {
    int result_rows = ceil(m.nrow() / (float) (downsample_factor));
    int result_columns = ceil(m.ncolumn() / (float) (downsample_factor));
    Matrix2F scalar_result(result_rows, result_columns);
    BlockSmoothScalar(m.data(), scalar_result.data(), m.ncolumn(), m.nrow(), scalar_result.ncolumn(), scalar_result.nrow(), 0, 0, downsample_factor);
    return std::move(scalar_result);
}

Matrix2F DownsampleTileSSE(const Matrix2F& m, int downsample_factor) {
    int result_rows = ceil(m.nrow() / (float) (downsample_factor));
    int result_columns = ceil(m.ncolumn() / (float) (downsample_factor));
    Matrix2F scalar_result(result_rows, result_columns);
    BlockSmoothSSE(m.data(), scalar_result.data(), m.ncolumn(), m.nrow(), scalar_result.ncolumn(), scalar_result.nrow(), 0, 0, downsample_factor);
    return std::move(scalar_result);
}

#ifdef __AVX__
Matrix2F DownsampleTileAVX(const Matrix2F& m, int downsample_factor) {
    int result_rows = ceil(m.nrow() / (float) (downsample_factor));
    int result_columns = ceil(m.ncolumn() / (float) (downsample_factor));
    Matrix2F scalar_result(result_rows, result_columns);
    BlockSmoothAVX(m.data(), scalar_result.data(), m.ncolumn(), m.nrow(), scalar_result.ncolumn(), scalar_result.nrow(), 0, 0, downsample_factor);
    return std::move(scalar_result);
}
#endif

TEST(BlockSmoothing, TestControl) {
    for (auto i = 0; i < NUM_ITERS; i++) {
        auto m1 = RandomMatrix(size_random(mt), size_random(mt));
        for (auto j = 4; j <= MAX_DOWNSAMPLE_FACTOR; j *= 2) {
            auto smoothed_scalar = DownsampleTileScalar(m1, j);
            auto smoothed_sse = DownsampleTileSSE(m1, j);
            Matrix2F res = abs(smoothed_scalar - smoothed_sse);
            auto s_delta = sum(res);
            auto m_delta = max(res);

            EXPECT_GE(s_delta, 0.0);
            EXPECT_GE(m_delta, 0.0);
        }
    }
}

TEST(BlockSmoothing, TestSSEAccuracy) {
    for (auto i = 0; i < NUM_ITERS; i++) {
        auto m1 = RandomMatrix(size_random(mt), size_random(mt));
        for (auto j = 4; j <= MAX_DOWNSAMPLE_FACTOR; j *= 2) {
            auto smoothed_scalar = DownsampleTileScalar(m1, j);
            auto smoothed_sse = DownsampleTileSSE(m1, j);
            Matrix2F res = abs(smoothed_scalar - smoothed_sse);
            auto s_delta = sum(res);
            auto m_delta = max(res);

            EXPECT_LE(s_delta, MAX_SUM_ERROR);
            EXPECT_LE(m_delta, MAX_ABS_ERROR);
        }
    }
}

TEST(BlockSmoothing, TestSSEPerformance) {
    Timer t;
    for (auto i = 0; i < NUM_ITERS; i++) {
        auto m1 = RandomMatrix(size_random(mt), size_random(mt));
        for (auto j = 4; j <= MAX_DOWNSAMPLE_FACTOR; j *= 2) {
            t.Start("scalar");
            auto smoothed_scalar = DownsampleTileScalar(m1, j);
            t.End("scalar");
            t.Start("simd");
            auto smoothed_simd = DownsampleTileSSE(m1, j);
            t.End("simd");
        }
    }
    auto scalar_time = t.GetMeasurement("scalar");
    auto simd_time = t.GetMeasurement("simd");
    double speedup = scalar_time / simd_time;
    EXPECT_GE(speedup, MINIMUM_SPEEDUP);
}


#ifdef __AVX__

TEST(BlockSmoothing, TestAVXAccuracy) {
    for (auto i = 0; i < NUM_ITERS; i++) {
        auto m1 = RandomMatrix(size_random(mt), size_random(mt));
        for (auto j = 8; j <= MAX_DOWNSAMPLE_FACTOR; j *= 2) {
            auto smoothed_scalar = DownsampleTileScalar(m1, j);
            auto smoothed_avx = DownsampleTileAVX(m1, j);
            Matrix2F res = abs(smoothed_scalar - smoothed_avx);
            auto s_delta = sum(res);
            auto m_delta = max(res);

            EXPECT_LE(s_delta, MAX_SUM_ERROR);
            EXPECT_LE(m_delta, MAX_ABS_ERROR);
        }
    }
}

TEST(BlockSmoothing, TestAVXPerformance) {
    Timer t;
    for (auto i = 0; i < NUM_ITERS; i++) {
        auto m1 = RandomMatrix(size_random(mt), size_random(mt));
        for (auto j = 8; j <= MAX_DOWNSAMPLE_FACTOR; j *= 2) {
            t.Start("scalar");
            auto smoothed_scalar = DownsampleTileScalar(m1, j);
            t.End("scalar");
            t.Start("simd");
            auto smoothed_simd = DownsampleTileAVX(m1, j);
            t.End("simd");
        }
    }
    auto scalar_time = t.GetMeasurement("scalar");
    auto simd_time = t.GetMeasurement("simd");
    double speedup = scalar_time / simd_time;
    EXPECT_GE(speedup, MINIMUM_SPEEDUP);
}

#endif