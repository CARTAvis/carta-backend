/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
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

// return value from gaussian distribution
double NormPdf(double x, double sigma) {
    return exp(-0.5 * x * x / (sigma * sigma)) / sigma;
}

// get array with gaussian distribution, with mean at the middle of the array
void MakeKernel(vector<float>& kernel, double sigma) {
    const int kernel_radius = (kernel.size() - 1) / 2;
    for (int j = 0; j <= kernel_radius; ++j) {
        kernel[kernel_radius + j] = kernel[kernel_radius - j] = NormPdf(j, sigma);
    }
}

// apply kernel to data
// the vertical parameter specifies if the kernel is applied in the vertical direction or in the horizontal direction
bool RunKernel(const vector<float>& kernel, const float* src_data, float* dest_data, const int64_t src_width, const int64_t src_height,
    const int64_t dest_width, const int64_t dest_height, const bool vertical) {
    const int64_t kernel_radius = (kernel.size() - 1) / 2;

    // if vertical, check if dest_height is large enough, else only check width
    if (vertical && dest_height < src_height - kernel_radius * 2) {
        return false;
    }

    // width is always checked
    if (dest_width < src_width - kernel_radius * 2) {
        return false;
    }

    // if vertical, the jump size is src_width, else 1
    const int64_t jump_size = vertical ? src_width : 1;
    // round dest_width to the nearest multiple of SIMD_WIDTH, to run on parallel in steps of SIMD_WIDTH (4 or 8)
    const int64_t dest_block_limit = SIMD_WIDTH * ((dest_width) / SIMD_WIDTH);
    const int64_t x_offset = vertical ? 0 : kernel_radius;
    const int64_t y_offset = vertical ? kernel_radius : 0;

    // manage threads
    ThreadManager::ApplyThreadLimit();
    // Run on parallel threads
#pragma omp parallel for
    // define src_y and src_x to run over src_data in steps of 4 or 8 (SMID_WIDTH)
    for (int64_t dest_y = 0; dest_y < dest_height; dest_y++) {
        int64_t src_y = dest_y + y_offset;
        // Handle row in steps of 4 or 8 using SSE or AVX (see HEADER)
        for (int64_t dest_x = 0; dest_x < dest_block_limit; dest_x += SIMD_WIDTH) {
            int64_t dest_index = dest_x + dest_width * dest_y;
            int64_t src_x = dest_x + x_offset;
// this if else block applies the kernel to the data
#ifdef __AVX__
            // __m256 is a vectorized data type that can hold 8 floats (256 bits)
            // _mm256_setzero_ps() sets all 8 floats to 0
            __m256 sum = _mm256_setzero_ps();
            __m256 weight = _mm256_setzero_ps();
            for (int64_t i = -kernel_radius; i <= kernel_radius; i++) {
                // define index of src_data to apply kernel
                int64_t src_index = src_x + i * jump_size + src_width * src_y;
                // load 8 floats from src_data at said index
                __m256 val = _mm256_loadu_ps(src_data + src_index);
                // load the required 8 floats from the kernel (looping)
                __m256 w = _mm256_set1_ps(kernel[i + kernel_radius]);
                // create a mask with infinites and NaNs (mm256_cmp_ps returns 1 if equal, 0 otherwise, also if both NaN)
                __m256 mask = _mm256_andnot_ps(IsInfinity(val), _mm256_cmp_ps(val, val, _CMP_EQ_OQ));
                // apply mask to kernel and src_data
                w = _mm256_and_ps(w, mask);
                val = _mm256_and_ps(val, mask);
                // sum the weighted values
                sum += val * w;
                weight += w;
            }
            // normalize
            sum /= weight;
            // store the result in dest_data at required index
            _mm256_storeu_ps(dest_data + dest_index, sum);
#else
            // __m128 is a vectorized data type that can hold 4 floats (128 bits)
            // _mm_setzero_ps() sets all 4 floats to 0
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

// apply gaussian smoothing to the data using previous functions
// smoothing factor is the size of the block to be averaged at the downsampling stage
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
    //create temporary array that will be deleted automatically when out of scope
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
        // Gaussian filters are separable, apply kernel in each direction (vertical and horizontal)
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

    auto dt = t.Elapsed();
    auto rate = dest_width * dest_height / dt.us();
    spdlog::performance("Smoothed with smoothing factor of {} and kernel size of {} in {:.3f} ms at {:.3f} MPix/s", smoothing_factor,
        mask_size, dt.ms(), rate);

    return true;
}

// blocksmooth functions downlample (averaging) the data, no gaussian kernel is applied

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

// ----------------------- 3D functions -----------------------
// X : width (column), Y : height (row), Z : depth (plane)
// vertical seems to be (NOOOO, ASK!) needed because the kernel needs to be applied in both directions, thats why RunKernel is called twice in GaussianSmooth, one with vertical = true and one with vertical = false

//RunKernel3D. Should I use vertical? Not for now
bool RunKernel3D(const vector<float>& kernel, const float* src_data, float* dest_data,
    const int64_t src_width, const int64_t src_height, const int64_t src_depth,
    const int64_t dest_width, const int64_t dest_height, const int64_t dest_depth, const int axis) {
    // axis = 0 for x, 1 for y, 2 for z

    const int64_t kernel_radius = (kernel.size() - 1) / 2;

    if (dest_width < src_width - kernel_radius * 2) {
        return false;
    }

    int64_t x_offset = 0;
    int64_t y_offset = 0;
    int64_t z_offset = 0;
    int64_t jump_size = 1;
    if (axis == 0) {
        x_offset = kernel_radius;
    } else if (axis == 1) {
        y_offset = kernel_radius;
        jump_size = src_width;
        if (dest_height < src_height - kernel_radius * 2) {
            return false;
        }
    } else if (axis == 2) {
        z_offset = kernel_radius;
        jump_size = src_width * src_height;
        if (dest_depth < src_depth - kernel_radius * 2) {
            return false;
        }
    } else {
        spdlog::error("Invalid axis value. Should be 0, 1 or 2 (got {})", axis);
        return false;
    }

    // const int64_t dest_block_limit = SIMD_WIDTH * ((dest_width) / SIMD_WIDTH);

    // manage threads
    ThreadManager::ApplyThreadLimit();
#pragma omp parallel for
    for (int64_t dest_z = 0; dest_z < dest_depth; dest_z++) {
        int64_t src_z = dest_z + z_offset;
        for (int64_t dest_y = 0; dest_y < dest_height; dest_y++) {
            int64_t src_y = dest_y + y_offset;
            for (int64_t dest_x = 0; dest_x < dest_width; dest_x++) {
                int64_t src_x = dest_x + x_offset;
                int64_t dest_index = dest_x + dest_width * dest_y + dest_width * dest_height * dest_z;
                float sum = 0.0;
                float weight = 0.0;
                for (int64_t i = -kernel_radius; i <= kernel_radius; i++) {
                    int64_t src_index = src_x + i * jump_size + src_width * src_y + src_width * src_height * src_z;
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
        }


//     // manage threads
//     ThreadManager::ApplyThreadLimit();
//     // Run on parallel threads
// #pragma omp parallel for
//     for (int64_t dest_z = 0; dest_z < dest_depth; dest_z++) {
//         int64_t src_z = dest_z + z_offset;
//         // Handle row in steps of 4 or 8 using SSE or AVX
//         for (int64_t dest_y = 0; dest_y < dest_height; dest_y++) {
//             int64_t src_y = dest_y + y_offset;
//             for (int64_t dest_x = 0; dest_x < dest_block_limit; dest_x += SIMD_WIDTH) {
//                 int64_t dest_index = dest_x + dest_width * dest_y + dest_width * dest_height * dest_z;
//                 int64_t src_x = dest_x + x_offset;
// #ifdef __AVX__
//                 __m256 sum = _mm256_setzero_ps();
//                 __m256 weight = _mm256_setzero_ps();
//                 for (int64_t i = -kernel_radius; i <= kernel_radius; i++) {
//                     // take indexes the at the same order as dest_index
//                     int64_t src_index = src_x + i * jump_size + src_width * src_y + src_width * src_height * src_z;
//                     __m256 val = _mm256_loadu_ps(src_data + src_index);
//                     __m256 w = _mm256_set1_ps(kernel[i + kernel_radius]);
//                     __m256 mask = _mm256_andnot_ps(IsInfinity(val), _mm256_cmp_ps(val, val, _CMP_EQ_OQ));
//                     w = _mm256_and_ps(w, mask);
//                     val = _mm256_and_ps(val, mask);
//                     sum += val * w;
//                     weight += w;
//                 }
//                 sum /= weight;
//                 _mm256_storeu_ps(dest_data + dest_index, sum);
// #else
//                 __m128 sum = _mm_setzero_ps();
//                 __m128 weight = _mm_setzero_ps();
//                 for (int64_t i = -kernel_radius; i <= kernel_radius; i++) {
//                     int64_t src_index = src_x + i * jump_size + src_width * src_y + src_width * src_height * src_z;
//                     __m128 val = _mm_loadu_ps(src_data + src_index);
//                     __m128 w = _mm_set_ps1(kernel[i + kernel_radius]);
//                     __m128 mask = _mm_andnot_ps(IsInfinity(val), _mm_cmpeq_ps(val, val));
//                     w = _mm_and_ps(w, mask);
//                     val = _mm_and_ps(val, mask);
//                     sum += val * w;
//                     weight += w;
//                 }
//                 sum /= weight;
//                 _mm_storeu_ps(dest_data + dest_index, sum);
// #endif
//             }

//             // Handle remainder of each block
//             for (int64_t dest_x = dest_block_limit; dest_x < dest_width; dest_x++) {
//                 int64_t dest_index = dest_x + dest_width * dest_y + dest_width * dest_height * dest_z;
//                 int64_t src_x = dest_x + x_offset;
//                 float sum = 0.0;
//                 float weight = 0.0;
//                 for (int64_t i = -kernel_radius; i <= kernel_radius; i++) {
//                     int64_t src_index = src_x + i * jump_size + src_width * src_y + src_width * src_height * src_z;
//                     float val = src_data[src_index];
//                     if (!isnan(val)) {
//                         float w = kernel[i + kernel_radius];
//                         sum += val * w;
//                         weight += w;
//                     }
//                 }
//                 if (weight > 0.0) {
//                     sum /= weight;
//                 } else {
//                     sum = NAN;
//                 }
//                 dest_data[dest_index] = sum;
//             }
//         }
//     }
    return true;
}

bool GaussianSmooth3D(const float* src_data, float* dest_data, int64_t src_width,
    int64_t src_height, int64_t src_depth, int64_t dest_width, int64_t dest_height,
    int64_t dest_depth, int smoothing_factor) {
    float sigma = (smoothing_factor - 1) / 2.0f;
    int mask_size = (smoothing_factor - 1) * 2 + 1;
    const int apron_height = smoothing_factor - 1;
    int64_t calculated_dest_width = src_width - 2 * (smoothing_factor - 1);
    int64_t calculated_dest_height = src_height - 2 * (smoothing_factor - 1);
    int64_t calculated_dest_depth = src_depth - 2 * (smoothing_factor - 1);

    if (dest_width * dest_height * dest_width < calculated_dest_width * calculated_dest_height * calculated_dest_depth) {
        spdlog::error("Incorrectly sized destination array. Should be at least{}x{}x{} (got {}x{}x{})",
        calculated_dest_width, calculated_dest_height, calculated_dest_depth, dest_width, dest_height, dest_depth);
        return false;
    }
    std::vector<float> kernel(mask_size);
    MakeKernel(kernel, sigma);

    double target_pixels = (SMOOTHING_TEMP_BUFFER_SIZE_MB * 1e6) / sizeof(float);
    int64_t target_buffer_depth = pow(target_pixels, 1/3);
    int64_t target_buffer_height = pow(target_pixels, 1/3);
    int64_t target_buffer_width = pow(target_pixels, 1/3);
    if (target_buffer_width < 4 * apron_height) {
        target_buffer_width = 4 * apron_height;
    }
    if (target_buffer_height < 4 * apron_height) {
        target_buffer_height = 4 * apron_height;
    }
    if (target_buffer_depth < 4 * apron_height) {
        target_buffer_depth = 4 * apron_height;
    }
    int64_t buffer_width = min(target_buffer_width, src_width);
    int64_t buffer_height = min(target_buffer_height, src_height);
    int64_t buffer_depth = min(target_buffer_depth, src_depth);

    Timer t;
    std::unique_ptr<float[]> temp_array1(new float[buffer_width * buffer_height * buffer_depth]);
    std::unique_ptr<float[]> temp_array2(new float[buffer_width * buffer_height * buffer_depth]);
    auto source_ptr = src_data;
    auto dest_ptr = dest_data;
    const auto temp_ptr1 = temp_array1.get();
    const auto temp_ptr2 = temp_array2.get();

    int64_t plane_offset = 0;
    int64_t num_planes = buffer_depth - 2 * apron_height;

    // manage threads CAN I USE IT HERE??
//     ThreadManager::ApplyThreadLimit();
// #pragma omp parallel for
    for (int64_t k = 0; k <= (int)(dest_depth / buffer_depth); k++) {
        int64_t row_offset = 0;
        int64_t num_rows = buffer_height - 2 * apron_height;
        // clamp last iterations (depth)
        if (k == (int)(dest_depth / buffer_depth)) {
            num_planes = dest_depth - plane_offset;
        }
        for (int64_t j = 0; j <= (int)(dest_height / buffer_height); j++) {
            int64_t column_offset = 0;
            int64_t num_columns = buffer_width - 2 * apron_height;
            // clamp last iterations (height)
            if (j == (int)(dest_height / buffer_height)) {
                num_rows = dest_height - row_offset;
            }
            for (int64_t i = 0; i <= (int)(dest_width / buffer_width); i++) {
                // clamp last iterations (width)
                if (i == (int)(dest_width / buffer_width)) {
                    num_columns = dest_width - column_offset;
                }

                 // RUNKERNEL3D
                RunKernel3D(kernel, source_ptr, temp_ptr1, src_width, src_height, src_depth,
                num_columns + 2 * apron_height, num_rows + 2 * apron_height,
                num_planes + 2 * apron_height, 0);
                RunKernel3D(kernel, temp_array1.get(), temp_ptr2, buffer_width, buffer_height, buffer_depth, buffer_width, buffer_height, buffer_depth, 1);

                src_ptr += num_columns * i + src_width * num_rows * j + src_width * src_height * num_planes * k
                dest_ptr += num_columns * i + dest_width * num_rows * j + dest_width * dest_height * num_planes * k
                column_offset += num_rows;
            }
            row_offset += num_columns;
        }
        depth_offset += num_planes;
    }

    // Fill in original NaNs
    ThreadManager::ApplyThreadLimit();
#pragma omp parallel for
    for (int64_t k = 0; k < dest_depth; k++) {
        for (int64_t j = 0; j < dest_height; j++) {
            for (int64_t i = 0; i < dest_width; i++) {
                auto src_index = (k + apron_height) * src_width * src_height + (j + apron_height) * src_width + (i + apron_height);
                auto origVal = src_data[src_index];
                if (isnan(origVal)) {
                    dest_data[k * dest_width * dest_height + j * dest_width + i] = NAN;
                }
            }
        }
    }

    auto dt = t.Elapsed();
    auto rate = dest_width * dest_height * dest_depth / dt.us();
    spdlog::performance("Smoothed with smoothing factor of {} and kernel size of {} in {:.3f} ms at {:.3f} MPix/s", smoothing_factor,
        mask_size, dt.ms(), rate);

    return true;    
}

bool BlockSmooth3D(const float* src_data, float* dest_data, int64_t src_width, int64_t src_height,
    int64_t src_depth, int64_t dest_width, int64_t dest_height, int64_t dest_depth,
    int64_t x_offset, int64_t y_offset, int64_t z_offset, int smoothing_factor) {
#ifdef __AVX__
    // AVX version, only for 8x down-sampling and above
    if (smoothing_factor % 8 == 0) {
        return BlockSmoothAVX3D(src_data, dest_data, src_width, src_height, src_depth, dest_width,
            dest_height, dest_depth, x_offset, y_offset, z_offset, smoothing_factor);
    }
#endif
    // SSE2 version
    if (smoothing_factor % 4 == 0) {
        return BlockSmoothSSE3D(src_data, dest_data, src_width, src_height, src_depth, dest_width,
            dest_height, dest_depth, x_offset, y_offset, z_offset, smoothing_factor);
    } else {
        return BlockSmoothScalar3D(src_data, dest_data, src_width, src_height, src_depth,
            dest_width, dest_height, dest_depth, x_offset, y_offset, z_offset, smoothing_factor);
    }
}

#ifdef __AVX__
bool BlockSmoothAVX3D(const float* src_data, float* dest_data, int64_t src_width,
    int64_t src_height, int64_t src_depth, int64_t dest_width, int64_t dest_height,
    int64_t dest_depth, int64_t x_offset, int64_t y_offset, int64_t z_offset, int smoothing_factor) {
    ThreadManager::ApplyThreadLimit();
#pragma omp parallel for
    for (int64_t k = 0; k < dest_depth; ++k) {
        for (int64_t j = 0; j < dest_height; j++) {
            for (auto i = 0; i < dest_width; i++) {
                float pixel_sum = 0;
                float pixel_count = 0;
                int64_t image_plane = z_offset + (k * smoothing_factor);
                int64_t image_row = y_offset + (j * smoothing_factor);
                int64_t image_col = x_offset + (i * smoothing_factor);
                __m256 v0 = _mm256_setzero_ps();
                __m256 v1 = _mm256_set_ps1(1.0f);
                __m256 count = v0, total = v0;

                int planes_left = min(smoothing_factor, (int)(src_depth - image_plane));
                int rows_left = min(smoothing_factor, (int)(src_height - image_row));
                int columns_left = min(smoothing_factor, (int)(src_width - image_col));
                int blocks_left = columns_left * planes_left / 8;

                for (auto plane_index = 0; plane_index < planes_left; plane_index++) {
                    for (auto row_index = 0; row_index < rows_left; row_index++) {
                        const float* ptr = src_data + ((image_plane + plane_index) * src_width * src_height) + ((image_row + row_index) * src_width) + image_col;
                        for (auto col_index = 0; col_index < blocks_left; col_index++) {
                            __m256 row = _mm256_loadu_ps(ptr);
                            __m256 mask = _mm256_andnot_ps(IsInfinity(row), _mm256_cmp_ps(row, row, _CMP_EQ_OQ));
                            row = _mm256_and_ps(row, mask);
                            count = _mm256_add_ps(count, _mm256_and_ps(v1, mask));
                            total = _mm256_add_ps(total, row);
                            ptr += 8;
                        }
                    }
                }

                // reduce
                pixel_sum = _mm256_reduce_add_ps(total);
                pixel_count = _mm256_reduce_add_ps(count);

                if (columns_left != smoothing_factor || blocks_left != smoothing_factor) {
                    // Add edges of the block
                    for (auto plane_index = 0; plane_index < planes_left; plane_index++) {
                        for (auto row_index = 0; row_index < rows_left; row_index++) {
                            for (auto col_index = blocks_left * 8; col_index < columns_left; col_index++) {
                                auto pix_val = src_data[(image_plane + plane_index) * src_width * src_height + (image_row + row_index) * src_width + image_col + col_index];
                                if (std::isfinite(pix_val)) {
                                    pixel_count++;
                                    pixel_sum += pix_val;
                                }
                            }
                        }
                    }
                }
                dest_data[k * dest_width * dest_height + j * dest_width + i] = pixel_count ? pixel_sum / pixel_count : NAN;
            }
        }
    }
    return true;
}
#endif

bool BlockSmoothSSE3D(const float* src_data, float* dest_data, int64_t src_width, int64_t src_height,
    int64_t src_depth, int64_t dest_width, int64_t dest_height, int64_t dest_depth,
    int64_t x_offset, int64_t y_offset, int64_t z_offset, int smoothing_factor) {

// Smoothing factor is width of block to average. A cube of 4x4x4=64 will have smoothing factor=4

// can't do 3D with SEE?? only AVX?
    ThreadManager::ApplyThreadLimit();
#pragma omp parallel for
    for (int64_t k = 0; k < dest_depth; ++k) {
        for (int64_t j = 0; j < dest_height; j++) {
            for (auto i = 0; i < dest_width; i++) {
                float pixel_sum = 0;
                float pixel_count = 0;
                int64_t image_plane = z_offset + (k * smoothing_factor);
                int64_t image_row = y_offset + (j * smoothing_factor);
                int64_t image_col = x_offset + (i * smoothing_factor);
                __m128 v0 = _mm_setzero_ps();
                __m128 v1 = _mm_set_ps1(1.0f);
                __m128 count = v0, total = v0;

                int planes_left = min(smoothing_factor, (int)(src_depth - image_plane));
                int rows_left = min(smoothing_factor, (int)(src_height - image_row));
                int columns_left = min(smoothing_factor, (int)(src_width - image_col));
                int blocks_left = columns_left * planes_left / 4;

                for (auto plane_index = 0; plane_index < planes_left; plane_index++) {
                    for (auto row_index = 0; row_index < rows_left; row_index++) {
                        const float* ptr = src_data + ((image_plane + plane_index) * src_width * src_height) + ((image_row + row_index) * src_width) + image_col;
                        for (auto col_index = 0; col_index < blocks_left; col_index++) {
                            __m128 row = _mm_loadu_ps(ptr);
                            __m128 mask = _mm_andnot_ps(IsInfinity(row), _mm_cmpeq_ps(row, row));
                            row = _mm_and_ps(row, mask);
                            count = _mm_add_ps(count, _mm_and_ps(v1, mask));
                            total = _mm_add_ps(total, row);
                            ptr += 4;
                        }
                    }
                }

                // reduce
                total = _mm_hadd_ps(total, total);
                total = _mm_hadd_ps(total, total);
                _mm_store_ss(&pixel_sum, total);

                count = _mm_hadd_ps(count, count);
                count = _mm_hadd_ps(count, count);
                _mm_store_ss(&pixel_count, count);

                if (columns_left != smoothing_factor || blocks_left != smoothing_factor) {
                    // Add edges of the block
                    for (auto plane_index = 0; plane_index < planes_left; plane_index++) {
                        for (auto row_index = 0; row_index < rows_left; row_index++) {
                            for (auto col_index = blocks_left * 4; col_index < columns_left; col_index++) {
                                auto pix_val = src_data[(image_plane + plane_index) * src_width * src_height + (image_row + row_index) * src_width + image_col + col_index];
                                if (std::isfinite(pix_val)) {
                                    pixel_count++;
                                    pixel_sum += pix_val;
                                }
                            }
                        }
                    }
                }

                dest_data[k * dest_width * dest_height + j * dest_width + i] = pixel_count ? pixel_sum / pixel_count : NAN;
            }
        }
    }
    return true;
}

bool BlockSmoothScalar3D(const float* src_data, float* dest_data, int64_t src_width,
    int64_t src_height, int64_t src_depth, int64_t dest_width, int64_t dest_height,
    int64_t dest_depth, int64_t x_offset, int64_t y_offset, int64_t z_offset, int smoothing_factor) {
    ThreadManager::ApplyThreadLimit();
#pragma omp parallel for
    for (int64_t k = 0; k < dest_depth; ++k) {
        for (int64_t j = 0; j < dest_height; j++) {
            for (int64_t i = 0; i != dest_width; ++i) {
                float pixel_sum = 0;
                int pixel_count = 0;
                int64_t image_plane = z_offset + (k * smoothing_factor);
                int64_t image_row = y_offset + (j * smoothing_factor);
                int64_t image_col = x_offset + (i * smoothing_factor);
                auto planes_left = min(smoothing_factor, (int)(src_depth - image_plane));
                auto rows_left = min(smoothing_factor, (int)(src_height - image_row));
                auto cols_left = min(smoothing_factor, (int)(src_width - image_col));
                for (int64_t pixel_z = 0; pixel_z < planes_left; pixel_z++) {
                    for (int64_t pixel_y = 0; pixel_y < rows_left; pixel_y++) {
                        for (int64_t pixel_x = 0; pixel_x < cols_left; pixel_x++) {
                            float pix_val = src_data[(image_plane * src_width * src_height) + (image_row * src_width) + image_col];
                            if (std::isfinite(pix_val)) {
                                pixel_count++;
                                pixel_sum += pix_val;
                            }
                            image_col++;
                        }
                        image_row++;
                    }
                    image_plane++;
                }
                dest_data[k * dest_width * dest_height + j * dest_width + i] = pixel_count ? pixel_sum / pixel_count : NAN;
            }
        }
    }
    return true;
}

void NearestNeighbor3D(const float* src_data, float* dest_data, int64_t src_width,
    int64_t src_height, int64_t dest_width, int64_t dest_height, int64_t dest_depth,
    int64_t x_offset, int64_t y_offset, int64_t z_offset, int smoothing_factor) {
    ThreadManager::ApplyThreadLimit();
#pragma omp parallel for
    for (size_t k = 0; k < dest_depth; ++k) {
        for (size_t j = 0; j < dest_height; ++j) {
            for (auto i = 0; i < dest_width; i++) {
                auto image_plane = z_offset + k * smoothing_factor;
                auto image_row = y_offset + j * smoothing_factor;
                auto image_col = x_offset + i * smoothing_factor;
                dest_data[k * dest_width * dest_height + j * dest_width + i] = src_data[(image_plane * src_width * src_height) + (image_row * src_width) + image_col];
            }
        }
    }
}

} // namespace carta
