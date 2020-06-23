#include "Smoothing.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <vector>

#include <fmt/ostream.h>
#include <tbb/tbb.h>

using namespace std;

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
    int smoothing_factor, bool verbose_logging) {
    float sigma = (smoothing_factor - 1) / 2.0f;
    int mask_size = (smoothing_factor - 1) * 2 + 1;
    const int apron_height = smoothing_factor - 1;
    int64_t calculated_dest_width = src_width - 2 * (smoothing_factor - 1);
    int64_t calculated_dest_height = src_height - 2 * (smoothing_factor - 1);

    if (dest_width * dest_height < calculated_dest_width * calculated_dest_height) {
        fmt::print(std::cerr, "Incorrectly sized destination array. Should be at least{}x{} (got {}x{})\n", calculated_dest_width,
            calculated_dest_height, dest_width, dest_height);
        return false;
    }

    vector<float> kernel(mask_size);
    MakeKernel(kernel, sigma);

    double target_pixels = (SMOOTHING_TEMP_BUFFER_SIZE_MB * 1e6) / sizeof(float);
    int64_t target_buffer_height = target_pixels / dest_width;
    if (target_buffer_height < 4 * apron_height) {
        target_buffer_height = 4 * apron_height;
    }
    int64_t buffer_height = min(target_buffer_height, src_height);

    int64_t line_offset = 0;
    auto t_start = std::chrono::high_resolution_clock::now();
    unique_ptr<float> temp_array(new float[dest_width * buffer_height]);
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
    if (verbose_logging) {
        auto t_end = std::chrono::high_resolution_clock::now();
        auto dt = std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_start).count();
        auto rate = dest_width * dest_height / (double)dt;
        fmt::print("Smoothed with smoothing factor of {} and kernel size of {} in {} ms at {} MPix/s\n", smoothing_factor, mask_size,
            dt * 1e-3, rate);
    }
    return true;
}
