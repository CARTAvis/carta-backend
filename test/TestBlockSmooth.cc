/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <functional>
#include <random>
#include <tuple>
#include <vector>

#include <casa/Arrays/ArrayMath.h>
#include <casa/Arrays/Matrix.h>
#include <gtest/gtest.h>

#include "DataStream/Smoothing.h"

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

typedef casacore::Matrix<float> Matrix2F;

class BlockSmoothingTest : public ::testing::Test {
public:
    const std::vector<float> nan_fractions = {0.0f, 0.05f, 0.1f, 0.5f, 0.95f, 1.0f};

    std::random_device rd;
    std::mt19937 mt;
    std::uniform_real_distribution<float> float_random;
    std::uniform_int_distribution<int> size_random;

    // Test fixture constructor for BlockSmoothingTest.
    // Initializes random number generators for use in smoothing tests:
    //
    //  1. `mt`: a Mersenne Twister RNG seeded with a random device.
    //  2. `float_random`: generates pixel values uniformly in the range [0, 1.0].
    //  3. `size_random`: generates random image dimensions uniformly in the range [512, 1024].
    //
    // These randomized values allow BlockSmoothing tests to simulate different
    // image sizes and pixel distributions, ensuring robustness of the smoothing
    // algorithm across varied input conditions.
    BlockSmoothingTest() {
        mt = std::mt19937(rd());
        float_random = std::uniform_real_distribution<float>(0, 1.0f);
        // Random image widths and heights in range [512, 1024]
        size_random = std::uniform_int_distribution<int>(512, 1024);
    }

    // Helper function for generating randomized test matrices with controlled
    // fractions of invalid values.
    //
    // Arguments:
    //   - rows, columns: matrix dimensions.
    //   - nan_fraction: probability of assigning NaN or Infinity to a given cell.
    //
    // Behavior:
    //   * Each element of the matrix is randomly assigned as follows:
    //       - With probability `nan_fraction`: NaN
    //       - Else with probability `nan_fraction`: +Infinity
    //       - Otherwise: a random float in the range [-0.5, 0.5).
    //
    // The resulting matrix simulates realistic test data containing both valid and
    // invalid floating-point values, useful for verifying algorithm robustness
    // under noisy or corrupted input conditions.
    Matrix2F RandomMatrix(size_t rows, size_t columns, float nan_fraction) {
        Matrix2F m(rows, columns);

        for (auto i = 0; i < m.nrow(); i++) {
            for (auto j = 0; j < m.ncolumn(); j++) {
                if (float_random(mt) < nan_fraction) {
                    m(i, j) = NAN;
                } else if (float_random(mt) < nan_fraction) {
                    m(i, j) = INFINITY;
                } else {
                    m(i, j) = float_random(mt) - 0.5f;
                }
            }
        }
        return std::move(m);
    }

    // Utility function that checks whether a matrix contains only non-finite values.
    //
    // Behavior:
    //   * Iterates through all elements of the given Matrix2F.
    //   * If any element is finite (i.e., not NaN and not ±Infinity),
    //     the function returns false.
    //   * If all elements are non-finite, the function returns true.
    //
    // This function is useful in tests to quickly verify whether an operation
    // produced a completely invalid matrix (e.g., filled with NaNs/Infinities)
    // as opposed to one containing valid numeric results.
    bool IsNAN(const Matrix2F& m) {
        for (auto i = 0; i < m.nrow(); i++) {
            for (auto j = 0; j < m.ncolumn(); j++) {
                if (std::isfinite(m(i, j))) {
                    return false;
                }
            }
        }
        return true;
    }

    // Utility function that compares two matrices for matching validity patterns.
    //
    // Behavior:
    //   * Iterates through each element of the given matrices (assumed to have the
    //     same dimensions).
    //   * At each position, checks whether both values are finite or both are
    //     non-finite (NaN/±Infinity).
    //   * If any mismatch is found (e.g., one finite and one non-finite), the
    //     function returns false.
    //   * Returns true only if both matrices share the same finite/non-finite
    //     pattern across all elements.
    //
    // This helper is useful in tests to confirm that two results handle invalid
    // values consistently, even if the actual numeric contents differ.
    bool MatchingNANs(const Matrix2F& m1, const Matrix2F& m2) {
        for (auto i = 0; i < m1.nrow(); i++) {
            for (auto j = 0; j < m1.ncolumn(); j++) {
                if (std::isfinite(m1(i, j)) != std::isfinite(m2(i, j))) {
                    return false;
                }
            }
        }
        return true;
    }

    // Utility function that computes the sum of all finite values in a matrix,
    // ignoring NaNs and ±Infinity.
    //
    // Behavior:
    //   * Iterates through each element of the given Matrix2F.
    //   * If a value is finite, it is added to the running sum.
    //   * If no finite values are found, the function returns NaN.
    //   * Otherwise, it returns the sum of all finite elements.
    //
    // This helper is useful in tests to validate algorithms that must tolerate
    // NaN/Infinity values, allowing the sum of "real" values to be computed while
    // disregarding invalid data.
    float nansum(const Matrix2F& m) {
        float sum = 0;
        bool has_vals = false;
        for (auto i = 0; i < m.nrow(); i++) {
            for (auto j = 0; j < m.ncolumn(); j++) {
                auto val = m(i, j);
                if (std::isfinite(val)) {
                    has_vals = true;
                    sum += val;
                }
            }
        }
        return has_vals ? sum : NAN;
    }

    // Utility function that computes the maximum finite value in a matrix,
    // ignoring NaNs and ±Infinity.
    //
    // Behavior:
    //   * Iterates through all elements of the given Matrix2F.
    //   * Tracks the maximum among finite values.
    //   * If no finite values are found, the function returns NaN.
    //   * Otherwise, it returns the maximum finite element.
    //
    // This helper is useful in tests for validating algorithms that must handle
    // datasets with invalid values, ensuring that maximum calculations are robust
    // against NaNs and Infinities.
    float nanmax(const Matrix2F& m) {
        float max_val = std::numeric_limits<float>::lowest();
        bool has_vals = false;
        for (auto i = 0; i < m.nrow(); i++) {
            for (auto j = 0; j < m.ncolumn(); j++) {
                auto val = m(i, j);
                if (std::isfinite(val)) {
                    has_vals = true;
                    max_val = std::max(max_val, val);
                }
            }
        }
        return has_vals ? max_val : NAN;
    }

    // Helper function to downsample a matrix using scalar block smoothing.
    //
    // Arguments:
    //   - m: the input matrix to downsample.
    //   - downsample_factor: the factor by which to reduce the resolution.
    //
    // Behavior:
    //   * Computes the size of the downsampled matrix based on the input dimensions
    //     and the downsample factor.
    //   * Allocates a new Matrix2F for the downsampled result.
    //   * Calls BlockSmoothScalar to fill the downsampled matrix by averaging
    //     blocks of the original matrix.
    //   * Returns the downsampled matrix.
    //
    // This function is useful for testing image-processing or smoothing algorithms
    // on reduced-resolution data while preserving the overall intensity patterns.
    Matrix2F DownsampleTileScalar(const Matrix2F& m, int downsample_factor) {
        int result_rows = ceil(m.nrow() / (float)(downsample_factor));
        int result_columns = ceil(m.ncolumn() / (float)(downsample_factor));
        Matrix2F scalar_result(result_rows, result_columns);
        BlockSmoothScalar(
            m.data(), scalar_result.data(), m.ncolumn(), m.nrow(), scalar_result.ncolumn(), scalar_result.nrow(), 0, 0, downsample_factor);
        return std::move(scalar_result);
    }

    // Helper function to downsample a matrix using SSE-optimized block smoothing.
    //
    // Arguments:
    //   - m: the input matrix to downsample.
    //   - downsample_factor: the factor by which to reduce resolution.
    //
    // Behavior:
    //   * Computes the size of the downsampled matrix based on the input dimensions
    //     and the downsample factor.
    //   * Allocates a new Matrix2F for the downsampled result.
    //   * Calls BlockSmoothSSE to fill the downsampled matrix by averaging blocks
    //     of the original matrix using SIMD instructions (SSE).
    //   * Returns the downsampled matrix.
    //
    // This function is useful for testing or benchmarking downsampling operations
    // on matrices with high performance requirements, ensuring correctness while
    // leveraging SIMD acceleration.
    Matrix2F DownsampleTileSSE(const Matrix2F& m, int downsample_factor) {
        int result_rows = ceil(m.nrow() / (float)(downsample_factor));
        int result_columns = ceil(m.ncolumn() / (float)(downsample_factor));
        Matrix2F scalar_result(result_rows, result_columns);
        BlockSmoothSSE(
            m.data(), scalar_result.data(), m.ncolumn(), m.nrow(), scalar_result.ncolumn(), scalar_result.nrow(), 0, 0, downsample_factor);
        return std::move(scalar_result);
    }

#ifdef __AVX__
    // Helper function to downsample a matrix using AVX-optimized block smoothing.
    //
    // Arguments:
    //   - m: the input matrix to downsample.
    //   - downsample_factor: the factor by which to reduce resolution.
    //
    // Behavior:
    //   * Computes the size of the downsampled matrix based on the input dimensions
    //     and the downsample factor.
    //   * Allocates a new Matrix2F for the downsampled result.
    //   * Calls BlockSmoothAVX to fill the downsampled matrix by averaging blocks
    //     of the original matrix using SIMD instructions (AVX).
    //   * Returns the downsampled matrix.
    //
    // This function is useful for testing or benchmarking downsampling operations
    // on large matrices with high performance requirements, ensuring correctness
    // while leveraging AVX acceleration.
    Matrix2F DownsampleTileAVX(const Matrix2F& m, int downsample_factor) {
        int result_rows = ceil(m.nrow() / (float)(downsample_factor));
        int result_columns = ceil(m.ncolumn() / (float)(downsample_factor));
        Matrix2F scalar_result(result_rows, result_columns);
        BlockSmoothAVX(
            m.data(), scalar_result.data(), m.ncolumn(), m.nrow(), scalar_result.ncolumn(), scalar_result.nrow(), 0, 0, downsample_factor);
        return std::move(scalar_result);
    }
#endif
};

// Checks that scalar and SSE downsampling give results with identical NaN
// placement and non-negative absolute differences across multiple NaN fractions
// and downsample factors
// Expected: NaN masks match exactly; sum and max errors are ≥ 0 if finite
TEST_F(BlockSmoothingTest, TestControl) {
    for (auto nan_fraction : nan_fractions) {
        for (auto i = 0; i < NUM_ITERS; i++) {
            auto m1 = RandomMatrix(size_random(mt), size_random(mt), nan_fraction);
            for (auto j = 4; j <= MAX_DOWNSAMPLE_FACTOR; j *= 2) {
                auto smoothed_scalar = DownsampleTileScalar(m1, j);
                auto smoothed_sse = DownsampleTileSSE(m1, j);
                Matrix2F abs_diff = abs(smoothed_scalar - smoothed_sse);
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

struct DownsampleTestParams {
    float nan_fraction;
    int downsample_factor;
};

class DownsampleSSEAccuracyTest : public BlockSmoothingTest, public ::testing::WithParamInterface<DownsampleTestParams> {};

// Verifies that SSE downsampling matches scalar output within small numerical
// tolerances (≤ 1e-1 sum error, ≤ 1e-3 max error) for various NaN fractions
// and downsample factors
// Expected: NaN masks match exactly; both error metrics stay within limits
TEST_P(DownsampleSSEAccuracyTest, SSEvsScalar) {
    auto param = GetParam();
    for (auto i = 0; i < NUM_ITERS; i++) {
        auto m1 = RandomMatrix(size_random(mt), size_random(mt), param.nan_fraction);
        auto smoothed_scalar = DownsampleTileScalar(m1, param.downsample_factor);
        auto smoothed_sse = DownsampleTileSSE(m1, param.downsample_factor);
        Matrix2F abs_diff = abs(smoothed_scalar - smoothed_sse);
        auto sum_error = nansum(abs_diff);
        auto max_error = nanmax(abs_diff);
        EXPECT_EQ(MatchingNANs(smoothed_scalar, smoothed_sse), true);
        if (std::isfinite(sum_error)) {
            EXPECT_LE(sum_error, MAX_SUM_ERROR);
            EXPECT_LE(max_error, MAX_ABS_ERROR);
        }
    }
}

INSTANTIATE_TEST_SUITE_P(SSEAccuracy, DownsampleSSEAccuracyTest,
    ::testing::Values(DownsampleTestParams{0.0f, 4}, DownsampleTestParams{0.05f, 4}, DownsampleTestParams{0.1f, 4},
        DownsampleTestParams{0.5f, 4}, DownsampleTestParams{0.95f, 4}, DownsampleTestParams{1.0f, 4}, DownsampleTestParams{0.0f, 8},
        DownsampleTestParams{0.05f, 8}, DownsampleTestParams{0.1f, 8} // Add more combinations as needed
        ));

#ifdef COMPILE_PERFORMANCE_TESTS
// Parameterized performance test for SSE
struct DownsamplePerfParams {
    int downsample_factor;
};

class DownsampleSSEPerfTest : public BlockSmoothingTest, public ::testing::WithParamInterface<DownsamplePerfParams> {};

// Measures runtime of SSE vs scalar downsampling and confirms that SSE is at
// least 10% faster for different downsample factors
// Expected: Speedup ratio (scalar_time / sse_time) ≥ 1.1
TEST_P(DownsampleSSEPerfTest, SSEvsScalarSpeedup) {
    auto param = GetParam();
    carta::Timer t;
    for (auto i = 0; i < NUM_ITERS; i++) {
        auto m1 = RandomMatrix(size_random(mt), size_random(mt), 0);
        t.Start("scalar");
        auto smoothed_scalar = DownsampleTileScalar(m1, param.downsample_factor);
        t.End("scalar");
        t.Start("simd");
        auto smoothed_simd = DownsampleTileSSE(m1, param.downsample_factor);
        t.End("simd");
    }
    auto scalar_time = t.GetMeasurement("scalar");
    auto simd_time = t.GetMeasurement("simd");
    double speedup = scalar_time / simd_time;
    EXPECT_GE(speedup, MINIMUM_SPEEDUP);
}

INSTANTIATE_TEST_SUITE_P(SSEPerf, DownsampleSSEPerfTest,
    ::testing::Values(
        DownsamplePerfParams{4}, DownsamplePerfParams{8}, DownsamplePerfParams{16}, DownsamplePerfParams{32} // Add more factors as needed
        ));
#endif // COMPILE_PERFORMANCE_TESTS

#ifdef __AVX__

class DownsampleAVXAccuracyTest : public BlockSmoothingTest, public ::testing::WithParamInterface<DownsampleTestParams> {};

// Ensures AVX downsampling matches scalar output within the same error
// tolerances as SSE accuracy tests
// Expected: NaN masks match exactly; sum and max errors stay within limits
TEST_P(DownsampleAVXAccuracyTest, AVXvsScalar) {
    auto param = GetParam();
    for (auto i = 0; i < NUM_ITERS; i++) {
        auto m1 = RandomMatrix(size_random(mt), size_random(mt), param.nan_fraction);
        auto smoothed_scalar = DownsampleTileScalar(m1, param.downsample_factor);
        auto smoothed_avx = DownsampleTileAVX(m1, param.downsample_factor);
        Matrix2F abs_diff = abs(smoothed_scalar - smoothed_avx);
        auto sum_error = nansum(abs_diff);
        auto max_error = nanmax(abs_diff);
        EXPECT_EQ(MatchingNANs(smoothed_scalar, smoothed_avx), true);
        if (std::isfinite(sum_error)) {
            EXPECT_LE(sum_error, MAX_SUM_ERROR);
            EXPECT_LE(max_error, MAX_ABS_ERROR);
        }
    }
}

INSTANTIATE_TEST_SUITE_P(AVXAccuracy, DownsampleAVXAccuracyTest,
    ::testing::Values(DownsampleTestParams{0.0f, 8}, DownsampleTestParams{0.05f, 8}, DownsampleTestParams{0.1f, 8},
        DownsampleTestParams{0.5f, 8}, DownsampleTestParams{0.95f, 8}, DownsampleTestParams{1.0f, 8}, DownsampleTestParams{0.0f, 16},
        DownsampleTestParams{0.05f, 16} // Add more combinations as needed
        ));
#endif // __AVX__

#ifdef COMPILE_PERFORMANCE_TESTS

class DownsampleAVXPerfTest : public BlockSmoothingTest, public ::testing::WithParamInterface<DownsamplePerfParams> {};

// Compares AVX vs SSE runtime and confirms that AVX is at least 10% faster
// for various downsample factors
// Expected: Speedup ratio (sse_time / avx_time) ≥ 1.1
TEST_P(DownsampleAVXPerfTest, AVXvsSSESpeedup) {
    auto param = GetParam();
    carta::Timer t;
    for (auto i = 0; i < NUM_ITERS; i++) {
        auto m1 = RandomMatrix(size_random(mt), size_random(mt), 0);
        t.Start("sse");
        auto smoothed_sse = DownsampleTileSSE(m1, param.downsample_factor);
        t.End("sse");
        t.Start("avx");
        auto smoothed_avx = DownsampleTileAVX(m1, param.downsample_factor);
        t.End("avx");
    }
    auto sse_time = t.GetMeasurement("sse");
    auto avx_time = t.GetMeasurement("avx");
    double speedup = sse_time / avx_time;
    EXPECT_GE(speedup, MINIMUM_SPEEDUP);
}

INSTANTIATE_TEST_SUITE_P(AVXPerf, DownsampleAVXPerfTest,
    ::testing::Values(
        DownsamplePerfParams{8}, DownsamplePerfParams{16}, DownsamplePerfParams{32}, DownsamplePerfParams{64} // Add more factors as needed
        ));
#endif // COMPILE_PERFORMANCE_TESTS
