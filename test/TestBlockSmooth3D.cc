/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <random>
#include <vector>

#include <casa/Arrays/ArrayMath.h>
#include <casa/Arrays/Matrix.h>
#include <gtest/gtest.h>

#include "DataStream/Smoothing3D.h"

#ifdef COMPILE_PERFORMANCE_TESTS
#include <spdlog/fmt/fmt.h>
#include "Timer/Timer.h"
#endif

#define MAX_ABS_ERROR 1.0e-3f
#define MAX_SUM_ERROR 1.0e-1f

// Minimum speedup of 10% expected (SSE over scalar, AVX over SSE)
#define MINIMUM_SPEEDUP 1.1

#define NUM_ITERS 10
#define MAX_DOWNSAMPLE_FACTOR 256

using namespace carta;

// define the data type from casacore https://casacore.github.io/casacore/classcasacore_1_1Cube.html
typedef casacore::Cube<float> Cube3D;

class BlockSmoothing3DTest : public ::testing::Test {
public:
    const std::vector<float> nan_fractions = {0.0f, 0.05f, 0.1f, 0.5f, 0.95f, 1.0f};

    // random number generator
    std::random_device rd;
    // random number engine
    std::mt19937 mt;
    // declare float random number array
    std::uniform_real_distribution<float> float_random;
    // declare integer random number array
    std::uniform_int_distribution<int> size_random;

    BlockSmoothing3DTest() {
        mt = std::mt19937(rd());
        // get random float between 0 and 1
        float_random = std::uniform_real_distribution<float>(0, 1.0f);
        // Random image widths and heights in range [512, 1024]
        // get random integer between 512 and 1024
        size_random = std::uniform_int_distribution<int>(512, 1024);
    }

    Cube3D RandomCube(size_t rows, size_t columns, size_t planes, float nan_fraction) {
        Cube3d m(rows, columns, planes);

        for (auto i = 0; i < m.nrow(); i++) {
            for (auto j = 0; j < m.ncolumn(); j++) {
                for (auto k = 0; k < m.nplane(); k++) {
                    if (float_random(mt) < nan_fraction) {
                        m(i, j, k) = NAN;
                    } else if (float_random(mt) < nan_fraction) {
                        m(i, j, k) = INFINITY;
                    } else {
                        m(i, j, k) = float_random(mt) - 0.5f;
                    }
                }
            }
        }
        return std::move(m);
    }

    bool IsNAN(const Cube3D& m) {
        for (auto i = 0; i < m.nrow(); i++) {
            for (auto j = 0; j < m.ncolumn(); j++) {
                for (auto k = 0; k < m.nplane(); k++) {
                    if (std::isfinite(m(i, j, k))) {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    bool MatchingNANs(const Cube3D& m1, const Cube3D& m2) {
        for (auto i = 0; i < m1.nrow(); i++) {
            for (auto j = 0; j < m1.ncolumn(); j++) {
                for (auto k = 0; k < m1.nplane(); k++) {
                    if (std::isfinite(m1(i, j, k)) != std::isfinite(m2(i, j, k))) {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    float nansum(const Cube3D& m) {
        float sum = 0;
        bool has_vals = false;
        for (auto i = 0; i < m.nrow(); i++) {
            for (auto j = 0; j < m.ncolumn(); j++) {
                for (auto k = 0; k < m.nplane(); k++) {
                    auto val = m(i, j, k);
                    if (std::isfinite(val)) {
                        has_vals = true;
                        sum += val;
                    }
                }
            }
        }
        return has_vals ? sum : NAN;
    }

    float nanmax(const Cube3D& m) {
        float max_val = std::numeric_limits<float>::lowest();
        bool has_vals = false;
        for (auto i = 0; i < m.nrow(); i++) {
            for (auto j = 0; j < m.ncolumn(); j++) {
                for (auto k = 0; k < m.nplane(); k++) {
                    auto val = m(i, j, k);
                    if (std::isfinite(val)) {
                        has_vals = true;
                        max_val = std::max(max_val, val);
                    }
                }
            }
        }
        return has_vals ? max_val : NAN;
    }

    Cube3D DownsampleTileScalar(const Cube3D& m, int downsample_factor) {
        int result_rows = ceil(m.nrow() / (float)(downsample_factor));
        int result_columns = ceil(m.ncolumn() / (float)(downsample_factor));
        Cube3D scalar_result(result_rows, result_columns);
        BlockSmoothScalar(
            m.data(), scalar_result.data(), m.ncolumn(), m.nrow(), scalar_result.ncolumn(), scalar_result.nrow(), 0, 0, downsample_factor);
        return std::move(scalar_result);
    }

    Cube3D DownsampleTileSSE(const Cube3D& m, int downsample_factor) {
        int result_rows = ceil(m.nrow() / (float)(downsample_factor));
        int result_columns = ceil(m.ncolumn() / (float)(downsample_factor));
        Cube3D scalar_result(result_rows, result_columns);
        BlockSmoothSSE(
            m.data(), scalar_result.data(), m.ncolumn(), m.nrow(), scalar_result.ncolumn(), scalar_result.nrow(), 0, 0, downsample_factor);
        return std::move(scalar_result);
    }

#ifdef __AVX__
    Cube3D DownsampleTileAVX(const Cube3D& m, int downsample_factor) {
        int result_rows = ceil(m.nrow() / (float)(downsample_factor));
        int result_columns = ceil(m.ncolumn() / (float)(downsample_factor));
        Cube3D scalar_result(result_rows, result_columns);
        BlockSmoothAVX(
            m.data(), scalar_result.data(), m.ncolumn(), m.nrow(), scalar_result.ncolumn(), scalar_result.nrow(), 0, 0, downsample_factor);
        return std::move(scalar_result);
    }
#endif
};

TEST_F(BlockSmoothingTest, TestControl) {
    for (auto nan_fraction : nan_fractions) {
        for (auto i = 0; i < NUM_ITERS; i++) {
            auto m1 = RandomMatrix(size_random(mt), size_random(mt), nan_fraction);
            for (auto j = 4; j <= MAX_DOWNSAMPLE_FACTOR; j *= 2) {
                auto smoothed_scalar = DownsampleTileScalar(m1, j);
                auto smoothed_sse = DownsampleTileSSE(m1, j);
                Cube3D abs_diff = abs(smoothed_scalar - smoothed_sse);
                auto sum_error = nansum(abs_diff);
                auto max_error = nanmax(abs_diff);
                EXPECT_EQ(MatchingNANs(smoothed_scalar, smoothed_sse), true);
                if (std::isfinite(sum_error)) {
                    EXPECT_GE(sum_error, 0);
                    EXPECT_GE(max_error, 0);
                }
            }
        }
    }
}

TEST_F(BlockSmoothingTest, TestSSEAccuracy) {
    for (auto nan_fraction : nan_fractions) {
        for (auto i = 0; i < NUM_ITERS; i++) {
            auto m1 = RandomMatrix(size_random(mt), size_random(mt), nan_fraction);
            for (auto j = 4; j <= MAX_DOWNSAMPLE_FACTOR; j *= 2) {
                auto smoothed_scalar = DownsampleTileScalar(m1, j);
                auto smoothed_sse = DownsampleTileSSE(m1, j);
                Cube3D abs_diff = abs(smoothed_scalar - smoothed_sse);
                auto sum_error = nansum(abs_diff);
                auto max_error = nanmax(abs_diff);
                EXPECT_EQ(MatchingNANs(smoothed_scalar, smoothed_sse), true);
                if (std::isfinite(sum_error)) {
                    EXPECT_LE(sum_error, MAX_SUM_ERROR);
                    EXPECT_LE(max_error, MAX_ABS_ERROR);
                }
            }
        }
    }
}

#ifdef COMPILE_PERFORMANCE_TESTS
TEST_F(BlockSmoothingTest, TestSSEPerformance) {
    carta::Timer t;
    for (auto i = 0; i < NUM_ITERS; i++) {
        auto m1 = RandomMatrix(size_random(mt), size_random(mt), 0);
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
#endif

#ifdef __AVX__

TEST_F(BlockSmoothingTest, TestAVXAccuracy) {
    for (auto nan_fraction : nan_fractions) {
        for (auto i = 0; i < NUM_ITERS; i++) {
            auto m1 = RandomMatrix(size_random(mt), size_random(mt), nan_fraction);
            for (auto j = 8; j <= MAX_DOWNSAMPLE_FACTOR; j *= 2) {
                auto smoothed_scalar = DownsampleTileScalar(m1, j);
                auto smoothed_avx = DownsampleTileAVX(m1, j);
                Cube3D abs_diff = abs(smoothed_scalar - smoothed_avx);
                auto sum_error = nansum(abs_diff);
                auto max_error = nanmax(abs_diff);

                EXPECT_EQ(MatchingNANs(smoothed_scalar, smoothed_avx), true);
                if (std::isfinite(sum_error)) {
                    EXPECT_LE(sum_error, MAX_SUM_ERROR);
                    EXPECT_LE(max_error, MAX_ABS_ERROR);
                }
            }
        }
    }
}

#ifdef COMPILE_PERFORMANCE_TESTS
TEST_F(BlockSmoothingTest, TestAVXPerformance) {
    carta::Timer t;
    for (auto i = 0; i < NUM_ITERS; i++) {
        auto m1 = RandomMatrix(size_random(mt), size_random(mt), 0);
        for (auto j = 8; j <= MAX_DOWNSAMPLE_FACTOR; j *= 2) {
            t.Start("sse");
            auto smoothed_sse = DownsampleTileSSE(m1, j);
            t.End("sse");
            t.Start("avx");
            auto smoothed_avx = DownsampleTileAVX(m1, j);
            t.End("avx");
        }
    }
    auto sse_time = t.GetMeasurement("sse");
    auto avx_time = t.GetMeasurement("avx");
    double speedup = sse_time / avx_time;
    EXPECT_GE(speedup, MINIMUM_SPEEDUP);
}
#endif

#endif
