/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Compression.h"

#include <array>
#include <cmath>

#include <zfp.h>

#ifdef _ARM_ARCH_
#include <sse2neon/sse2neon.h>
#else
#include <x86intrin.h>
#endif

namespace carta {

int Compress(std::vector<float>& array, size_t offset, std::vector<char>& compression_buffer, size_t& compressed_size, uint32_t nx,
    uint32_t ny, uint32_t precision) {
    int status = 0;     /* return value: 0 = success */
    zfp_type type;      /* array scalar type */
    zfp_field* field;   /* array meta data */
    zfp_stream* zfp;    /* compressed stream */
    size_t buffer_size; /* byte size of compressed buffer */
    bitstream* stream;  /* bit stream to write to or read from */

    type = zfp_type_float;
    field = zfp_field_2d(array.data() + offset, type, nx, ny);

    /* allocate meta data for a compressed stream */
    zfp = zfp_stream_open(nullptr);

    /* set compression mode and parameters via one of three functions */
    zfp_stream_set_precision(zfp, precision);

    /* allocate buffer for compressed data */
    buffer_size = zfp_stream_maximum_size(zfp, field);
    if (compression_buffer.size() < buffer_size) {
        compression_buffer.resize(buffer_size);
    }
    stream = stream_open(compression_buffer.data(), buffer_size);
    zfp_stream_set_bit_stream(zfp, stream);
    zfp_stream_rewind(zfp);

    compressed_size = zfp_compress(zfp, field);
    if (!compressed_size) {
        status = 1;
    }

    /* clean up */
    zfp_field_free(field);
    zfp_stream_close(zfp);
    stream_close(stream);

    return status;
}

int Decompress(std::vector<float>& array, std::vector<char>& compression_buffer, int nx, int ny, int precision) {
    int status = 0;    /* return value: 0 = success */
    zfp_type type;     /* array scalar type */
    zfp_field* field;  /* array meta data */
    zfp_stream* zfp;   /* compressed stream */
    bitstream* stream; /* bit stream to write to or read from */
    type = zfp_type_float;
    array.resize(nx * ny); // resize the resulting data
    field = zfp_field_2d(array.data(), type, nx, ny);
    zfp = zfp_stream_open(NULL);

    zfp_stream_set_precision(zfp, precision);
    stream = stream_open(compression_buffer.data(), compression_buffer.size());
    zfp_stream_set_bit_stream(zfp, stream);
    zfp_stream_rewind(zfp);

    if (!zfp_decompress(zfp, field)) {
        status = 1;
    }

    zfp_field_free(field);
    zfp_stream_close(zfp);
    stream_close(stream);

    return status;
}

// Removes NaNs from an array and returns run-length encoded list of NaNs
std::vector<int32_t> GetNanEncodingsSimple(std::vector<float>& array, int offset, int length) {
    int32_t prev_index = offset;
    bool prev = false;
    std::vector<int32_t> encoded_array;
    // Find first non-NaN number in the array
    float prev_valid_num = 0;
    for (auto i = offset; i < offset + length; i++) {
        if (!std::isnan(array[i])) {
            prev_valid_num = array[i];
            break;
        }
    }

    // Generate RLE list and replace NaNs with neighbouring valid values. Ideally, this should take into account
    // the width and height of the image, and look for neighbouring values in vertical and horizontal directions,
    // but this is only an issue with NaNs right at the edge of images.
    for (auto i = offset; i < offset + length; i++) {
        bool current = std::isnan(array[i]);
        if (current != prev) {
            encoded_array.push_back(i - prev_index);
            prev_index = i;
            prev = current;
        }
        if (current) {
            array[i] = prev_valid_num;
        } else {
            prev_valid_num = array[i];
        }
    }
    encoded_array.push_back(offset + length - prev_index);
    return encoded_array;
}

std::vector<int32_t> GetNanEncodingsBlock(std::vector<float>& array, int offset, int w, int h) {
    // Generate RLE NaN list
    int length = w * h;
    int32_t prev_index = offset;
    bool prev = false;
    std::vector<int32_t> encoded_array;

    for (auto i = offset; i < offset + length; i++) {
        bool current = std::isnan(array[i]);
        if (current != prev) {
            encoded_array.push_back(i - prev_index);
            prev_index = i;
            prev = current;
        }
    }
    encoded_array.push_back(offset + length - prev_index);

    // Skip all-NaN images and NaN-free images
    if (encoded_array.size() > 1) {
        // Calculate average of 4x4 blocks (matching blocks used in ZFP), and replace NaNs with block average
        for (auto i = 0; i < w; i += 4) {
            for (auto j = 0; j < h; j += 4) {
                int block_start = offset + j * w + i;
                int valid_count = 0;
                float sum = 0;
                // Limit the block size when at the edges of the image
                int block_width = std::min(4, w - i);
                int block_height = std::min(4, h - j);
                for (int x = 0; x < block_width; x++) {
                    for (int y = 0; y < block_height; y++) {
                        float v = array[block_start + (y * w) + x];
                        if (!std::isnan(v)) {
                            valid_count++;
                            sum += v;
                        }
                    }
                }

                // Only process blocks which have at least one valid value AND at least one NaN. All-NaN blocks won't affect ZFP compression
                if (valid_count && valid_count != block_width * block_height) {
                    float average = sum / valid_count;
                    for (int x = 0; x < block_width; x++) {
                        for (int y = 0; y < block_height; y++) {
                            float v = array[block_start + (y * w) + x];
                            if (std::isnan(v)) {
                                array[block_start + (y * w) + x] = average;
                            }
                        }
                    }
                }
            }
        }
    }
    return encoded_array;
}

// This function transforms an array of 2D vertices from contour data in order to improve compression ratios
void RoundAndEncodeVertices(const std::vector<float>& array, std::vector<int32_t>& dest, float rounding_factor) {
    const int num_values = array.size();
    dest.resize(num_values);
    int i = 0;

    const int blocked_length = 4 * (num_values / 4);
    // Run through the vertices in groups of 4, rounding to the nearest Nth of a pixel
    for (i = 0; i < blocked_length; i += 4) {
        __m128 vertices_vector = _mm_loadu_ps(&array[i]);
        // If we prefer truncation, then _mm_cvttps_epi32 should be used instead
        __m128i rounded_vals = _mm_cvtps_epi32(vertices_vector * rounding_factor);
        _mm_store_si128((__m128i*)&dest[i], rounded_vals);
    }

    // Round the remaining pixels
    for (i = blocked_length; i < num_values; i++) {
        dest[i] = round(array[i] * rounding_factor);
    }

    EncodeIntegers(dest, true);
}

void EncodeIntegers(std::vector<int32_t>& array, bool strided) {
    const int num_values = array.size();
    const int blocked_length = 4 * (num_values / 4);
    alignas(16) const std::array<uint8_t, 16> shuffle_vals = {0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15};

    if (strided) {
        // Delta-encoding of neighbouring vertices to improve compression
        int last_x = 0;
        int last_y = 0;
        for (int64_t i = 0; i < num_values - 1; i += 2) {
            int current_x = array[i];
            int current_y = array[i + 1];
            array[i] = current_x - last_x;
            array[i + 1] = current_y - last_y;
            last_x = current_x;
            last_y = current_y;
        }
    } else {
        // Delta-encoding of neighbouring integers to improve compression
        int last = 0;
        for (int64_t i = 0; i < num_values; i++) {
            int current = array[i];
            array[i] = current - last;
            last = current;
        }
    }

    // Shuffle bytes in blocks of 126 bits (4 floats). The remaining bytes are left along
    for (int64_t i = 0; i < blocked_length; i += 4) {
        __m128i vals = _mm_loadu_si128((__m128i*)&array[i]);
        vals = _mm_shuffle_epi8(vals, *(__m128i*)shuffle_vals.data());
        _mm_store_si128((__m128i*)&array[i], vals);
    }
}

} // namespace carta
