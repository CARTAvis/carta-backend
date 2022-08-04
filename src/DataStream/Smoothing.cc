/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Smoothing.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <vector>

#include "../Logger/Logger.h"
#include "ThreadingManager/ThreadingManager.h"
#include "Timer/Timer.h"

namespace carta {

double NormPdf(double x, double sigma) {
    return exp(-0.5 * x * x / (sigma * sigma)) / sigma;
}

void MakeKernel(vector<float>& kernel, double sigma) {
    const int kernel_radius = (kernel.size() - 1) / 2;
    for (int j = 0; j <= kernel_radius; ++j) {
        kernel[kernel_radius + j] = kernel[kernel_radius - j] = NormPdf(j, sigma);
    }
}

bool RunKernel(const vector<float>& kernel, const float* src_data, float* dest_data, const int64_t src_width, const int64_t src_height,
    const int64_t dest_width, const int64_t dest_height, const bool vertical) {
    const int64_t kernel_radius = (kernel.size() - 1) / 2;

    if (vertical && dest_height < src_height - kernel_radius * 2) {
        return false;
    }

    if (dest_width < src_width - kernel_radius * 2) {
        return false;
    }

    const int64_t jump_size = vertical ? src_width : 1;
    const int64_t dest_block_limit = SIMD_WIDTH * ((dest_width) / SIMD_WIDTH);
    const int64_t x_offset = vertical ? 0 : kernel_radius;
    const int64_t y_offset = vertical ? kernel_radius : 0;

    ThreadManager::ApplyThreadLimit();
#pragma omp parallel for
    for (int64_t dest_y = 0; dest_y < dest_height; dest_y++) {
        int64_t src_y = dest_y + y_offset;
        // Handle row in steps of 4 or 8 using SSE or AVX
        for (int64_t dest_x = 0; dest_x < dest_block_limit; dest_x += SIMD_WIDTH) {
            int64_t dest_index = dest_x + dest_width * dest_y;
            int64_t src_x = dest_x + x_offset;
#ifdef __AVX__
            __m256 sum = _mm256_setzero_ps();
            __m256 weight = _mm256_setzero_ps();
            for (int64_t i = -kernel_radius; i <= kernel_radius; i++) {
                int64_t src_index = src_x + i * jump_size + src_width * src_y;
                __m256 val = _mm256_loadu_ps(src_data + src_index);
                __m256 w = _mm256_set1_ps(kernel[i + kernel_radius]);
                __m256 mask = _mm256_andnot_ps(IsInfinity(val), _mm256_cmp_ps(val, val, _CMP_EQ_OQ));
                w = _mm256_and_ps(w, mask);
                val = _mm256_and_ps(val, mask);
                sum += val * w;
                weight += w;
            }
            sum /= weight;
            _mm256_storeu_ps(dest_data + dest_index, sum);
#else
            __m128 sum = _mm_setzero_ps();
            __m128 weight = _mm_setzero_ps();
            for (int64_t i = -kernel_radius; i <= kernel_radius; i++) {
                int64_t src_index = src_x + i * jump_size + src_width * src_y;
                __m128 val = _mm_loadu_ps(src_data + src_index);
                __m128 w = _mm_set_ps1(kernel[i + kernel_radius]);
                __m128 mask = _mm_andnot_ps(IsInfinity(val), _mm_cmpeq_ps(val, val));
                w = _mm_and_ps(w, mask);
                val = _mm_and_ps(val, mask);
                sum += val * w;
                weight += w;
            }
            sum /= weight;
            _mm_storeu_ps(dest_data + dest_index, sum);
#endif
        }

        // Handle remainder of each block
        for (int64_t dest_x = dest_block_limit; dest_x < dest_width; dest_x++) {
            int64_t dest_index = dest_x + dest_width * dest_y;
            int64_t src_x = dest_x + x_offset;
            float sum = 0.0;
            float weight = 0.0;
            for (int64_t i = -kernel_radius; i <= kernel_radius; i++) {
                int64_t src_index = src_x + i * jump_size + src_width * src_y;
                float val = src_data[src_index];
                if (!isnan(val)) {
                    float w = kernel[i + kernel_radius];
                    sum += val * w;
                    weight += w;
                }
            }
            if (weight > 0.0) {
                sum /= weight;
            } else {
                sum = NAN;
            }
            dest_data[dest_index] = sum;
        }
    }

    return true;
}

bool GaussianSmooth(const float* src_data, float* dest_data, int64_t src_width, int64_t src_height, int64_t dest_width, int64_t dest_height,
    int smoothing_factor) {
    float sigma = (smoothing_factor - 1) / 2.0f;
    int mask_size = (smoothing_factor - 1) * 2 + 1;
    const int apron_height = smoothing_factor - 1;
    int64_t calculated_dest_width = src_width - 2 * (smoothing_factor - 1);
    int64_t calculated_dest_height = src_height - 2 * (smoothing_factor - 1);

    if (dest_width * dest_height < calculated_dest_width * calculated_dest_height) {
        spdlog::error("Incorrectly sized destination array. Should be at least{}x{} (got {}x{})", calculated_dest_width,
            calculated_dest_height, dest_width, dest_height);
        return false;
    }

    std::vector<float> kernel(mask_size);
    MakeKernel(kernel, sigma);

    double target_pixels = (SMOOTHING_TEMP_BUFFER_SIZE_MB * 1e6) / sizeof(float);
    int64_t target_buffer_height = target_pixels / dest_width;
    if (target_buffer_height < 4 * apron_height) {
        target_buffer_height = 4 * apron_height;
    }
    int64_t buffer_height = min(target_buffer_height, src_height);

    int64_t line_offset = 0;
    Timer t;
    std::unique_ptr<float[]> temp_array(new float[dest_width * buffer_height]);
    auto source_ptr = src_data;
    auto dest_ptr = dest_data;
    const auto temp_ptr = temp_array.get();

    while (line_offset < dest_height) {
        int64_t num_lines = buffer_height - 2 * apron_height;
        // clamp last iteration
        if (line_offset + num_lines > dest_height) {
            num_lines = dest_height - line_offset;
        }
        RunKernel(kernel, source_ptr, temp_ptr, src_width, src_height, dest_width, num_lines + 2 * apron_height, false);
        RunKernel(kernel, temp_array.get(), dest_ptr, dest_width, num_lines + 2 * apron_height, dest_width, num_lines, true);

        line_offset += num_lines;
        source_ptr += num_lines * src_width;
        dest_ptr += num_lines * dest_width;
    }

    // Fill in original NaNs
    ThreadManager::ApplyThreadLimit();
#pragma omp parallel for
    for (int64_t j = 0; j < dest_height; j++) {
        for (int64_t i = 0; i < dest_width; i++) {
            auto src_index = (j + apron_height) * src_width + (i + apron_height);
            auto origVal = src_data[src_index];
            if (isnan(origVal)) {
                dest_data[j * dest_width + i] = NAN;
            }
        }
    }

    auto dt = t.Elapsed(Timer::ms);
    auto rate = dest_width * dest_height / t.Elapsed(Timer::us);
    spdlog::performance(
        "Smoothed with smoothing factor of {} and kernel size of {} in {:.3f} ms at {:.3f} MPix/s", smoothing_factor, mask_size, dt, rate);

    return true;
}

bool BlockSmooth(const float* src_data, float* dest_data, int64_t src_width, int64_t src_height, int64_t dest_width, int64_t dest_height,
    int64_t x_offset, int64_t y_offset, int smoothing_factor) {
#ifdef __AVX__
    // AVX version, only for 8x down-sampling and above
    if (smoothing_factor % 8 == 0) {
        return BlockSmoothAVX(src_data, dest_data, src_width, src_height, dest_width, dest_height, x_offset, y_offset, smoothing_factor);
    }
#endif
    // SSE2 version
    if (smoothing_factor % 4 == 0) {
        return BlockSmoothSSE(src_data, dest_data, src_width, src_height, dest_width, dest_height, x_offset, y_offset, smoothing_factor);
    } else {
        return BlockSmoothScalar(src_data, dest_data, src_width, src_height, dest_width, dest_height, x_offset, y_offset, smoothing_factor);
    }
}

bool BlockSmoothSSE(const float* src_data, float* dest_data, int64_t src_width, int64_t src_height, int64_t dest_width, int64_t dest_height,
    int64_t x_offset, int64_t y_offset, int smoothing_factor) {
    ThreadManager::ApplyThreadLimit();
#pragma omp parallel for
    for (int64_t j = 0; j < dest_height; ++j) {
        for (auto i = 0; i < dest_width; i++) {
            float pixel_sum = 0;
            float pixel_count = 0;
            size_t image_row = y_offset + (j * smoothing_factor);
            size_t image_col = x_offset + (i * smoothing_factor);
            __m128 v0 = _mm_setzero_ps();
            __m128 v1 = _mm_set_ps1(1.0f);
            __m128 count = v0, total = v0;

            int rows_left = min(smoothing_factor, (int)(src_height - image_row));
            int columns_left = min(smoothing_factor, (int)(src_width - image_col));
            int blocks_left = columns_left / 4;

            for (auto row_index = 0; row_index < rows_left; row_index++) {
                const float* ptr = src_data + ((image_row + row_index) * src_width) + image_col;
                for (auto col_index = 0; col_index < blocks_left; col_index++) {
                    __m128 row = _mm_loadu_ps(ptr);
                    __m128 mask = _mm_andnot_ps(IsInfinity(row), _mm_cmpeq_ps(row, row));
                    row = _mm_and_ps(row, mask);
                    count = _mm_add_ps(count, _mm_and_ps(v1, mask));
                    total = _mm_add_ps(total, row);
                    ptr += 4;
                }
            }

            // reduce
            total = _mm_hadd_ps(total, total);
            total = _mm_hadd_ps(total, total);
            _mm_store_ss(&pixel_sum, total);

            count = _mm_hadd_ps(count, count);
            count = _mm_hadd_ps(count, count);
            _mm_store_ss(&pixel_count, count);

            if (columns_left != smoothing_factor) {
                // Add right edge of block
                for (auto row_index = 0; row_index < rows_left; row_index++) {
                    for (auto col_index = blocks_left * 4; col_index < columns_left; col_index++) {
                        auto pix_val = src_data[(image_row + row_index) * src_width + image_col + col_index];
                        if (std::isfinite(pix_val)) {
                            pixel_count++;
                            pixel_sum += pix_val;
                        }
                    }
                }
            }

            dest_data[j * dest_width + i] = pixel_count ? pixel_sum / pixel_count : NAN;
        }
    }
    return true;
}

#ifdef __AVX__
bool BlockSmoothAVX(const float* src_data, float* dest_data, int64_t src_width, int64_t src_height, int64_t dest_width, int64_t dest_height,
    int64_t x_offset, int64_t y_offset, int smoothing_factor) {
    ThreadManager::ApplyThreadLimit();
#pragma omp parallel for
    for (int64_t j = 0; j < dest_height; ++j) {
        for (auto i = 0; i < dest_width; i++) {
            float pixel_sum = 0;
            float pixel_count = 0;
            int64_t image_row = y_offset + (j * smoothing_factor);
            int64_t image_col = x_offset + (i * smoothing_factor);

            __m256 v0 = _mm256_setzero_ps();
            __m256 v1 = _mm256_set1_ps(1.0f);

            __m256 count = v0, total = v0;

            int rows_left = min(smoothing_factor, (int)(src_height - image_row));
            int columns_left = min(smoothing_factor, (int)(src_width - image_col));
            int blocks_left = columns_left / 8;

            for (auto row_index = 0; row_index < rows_left; row_index++) {
                const float* ptr = src_data + ((image_row + row_index) * src_width) + image_col;
                for (auto col_index = 0; col_index < blocks_left; col_index++) {
                    __m256 row = _mm256_loadu_ps(ptr);
                    __m256 mask = _mm256_andnot_ps(IsInfinity(row), _mm256_cmp_ps(row, row, _CMP_EQ_OQ));
                    row = _mm256_and_ps(row, mask);
                    count = _mm256_add_ps(count, _mm256_and_ps(v1, mask));
                    total = _mm256_add_ps(total, row);
                    ptr += 8;
                }
            }

            // reduce
            pixel_sum = _mm256_reduce_add_ps(total);
            pixel_count = _mm256_reduce_add_ps(count);

            if (columns_left != smoothing_factor) {
                // Add right edge of block
                for (auto row_index = 0; row_index < rows_left; row_index++) {
                    for (auto col_index = blocks_left * 8; col_index < columns_left; col_index++) {
                        auto pix_val = src_data[(image_row + row_index) * src_width + image_col + col_index];
                        if (std::isfinite(pix_val)) {
                            pixel_count++;
                            pixel_sum += pix_val;
                        }
                    }
                }
            }
            dest_data[j * dest_width + i] = pixel_count ? pixel_sum / pixel_count : NAN;
        }
    }
    return true;
}
#endif

bool BlockSmoothScalar(const float* src_data, float* dest_data, int64_t src_width, int64_t src_height, int64_t dest_width,
    int64_t dest_height, int64_t x_offset, int64_t y_offset, int smoothing_factor) {
    ThreadManager::ApplyThreadLimit();
    // Non-SIMD version. This could still be optimised to use SIMD in future
#pragma omp parallel for
    for (int64_t j = 0; j < dest_height; ++j) {
        for (int64_t i = 0; i != dest_width; ++i) {
            float pixel_sum = 0;
            int pixel_count = 0;
            int64_t image_row = y_offset + (j * smoothing_factor);
            auto rows_left = min(smoothing_factor, (int)(src_height - image_row));
            for (int64_t pixel_y = 0; pixel_y < rows_left; pixel_y++) {
                int64_t image_col = x_offset + (i * smoothing_factor);
                auto cols_left = min(smoothing_factor, (int)(src_width - image_col));
                for (int64_t pixel_x = 0; pixel_x < cols_left; pixel_x++) {
                    float pix_val = src_data[(image_row * src_width) + image_col];
                    if (std::isfinite(pix_val)) {
                        pixel_count++;
                        pixel_sum += pix_val;
                    }
                    image_col++;
                }
                image_row++;
            }
            dest_data[j * dest_width + i] = pixel_count ? pixel_sum / pixel_count : NAN;
        }
    }
    return true;
}

void NearestNeighbor(const float* src_data, float* dest_data, int64_t src_width, int64_t dest_width, int64_t dest_height, int64_t x_offset,
    int64_t y_offset, int smoothing_factor) {
    ThreadManager::ApplyThreadLimit();
#pragma omp parallel for
    for (size_t j = 0; j < dest_height; ++j) {
        for (auto i = 0; i < dest_width; i++) {
            auto image_row = y_offset + j * smoothing_factor;
            auto image_col = x_offset + i * smoothing_factor;
            dest_data[j * dest_width + i] = src_data[(image_row * src_width) + image_col];
        }
    }
}

} // namespace carta
