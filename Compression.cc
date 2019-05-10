#include "Compression.h"

#include <cmath>

#include <zfp.h>

using namespace std;

int Compress(vector<float>& array, size_t offset, vector<char>& compression_buffer, size_t& compressed_size, uint32_t nx, uint32_t ny,
    uint32_t precision) {
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

int Decompress(
    vector<float>& array, vector<char>& compression_buffer, size_t& compressed_size, uint32_t nx, uint32_t ny, uint32_t precision) {
    int status = 0;    /* return value: 0 = success */
    zfp_type type;     /* array scalar type */
    zfp_field* field;  /* array meta data */
    zfp_stream* zfp;   /* compressed stream */
    bitstream* stream; /* bit stream to write to or read from */

    /* allocate meta data for the 3D array a[nz][ny][nx] */
    type = zfp_type_float;
    field = zfp_field_2d(array.data(), type, nx, ny);

    /* allocate meta data for a compressed stream */
    zfp = zfp_stream_open(nullptr);
    zfp_stream_set_precision(zfp, precision);

    stream = stream_open(compression_buffer.data(), compressed_size);
    zfp_stream_set_bit_stream(zfp, stream);
    zfp_stream_rewind(zfp);

    if (!zfp_decompress(zfp, field)) {
        // fmt::print("decompression failed\n");
        status = 1;
    }
    /* clean up */
    zfp_field_free(field);
    zfp_stream_close(zfp);
    stream_close(stream);

    return status;
}

// Removes NaNs from an array and returns run-length encoded list of NaNs
vector<int32_t> GetNanEncodingsSimple(vector<float>& array, int offset, int length) {
    int32_t prev_index = offset;
    bool prev = false;
    vector<int32_t> encoded_array;
    // Find first non-NaN number in the array
    float prev_valid_num = 0;
    for (auto i = offset; i < offset + length; i++) {
        if (!isnan(array[i])) {
            prev_valid_num = array[i];
            break;
        }
    }

    // Generate RLE list and replace NaNs with neighbouring valid values. Ideally, this should take into account
    // the width and height of the image, and look for neighbouring values in vertical and horizontal directions,
    // but this is only an issue with NaNs right at the edge of images.
    for (auto i = offset; i < offset + length; i++) {
        bool current = isnan(array[i]);
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

vector<int32_t> GetNanEncodingsBlock(vector<float>& array, int offset, int w, int h) {
    // Generate RLE NaN list
    int length = w * h;
    int32_t prev_index = offset;
    bool prev = false;
    vector<int32_t> encoded_array;

    for (auto i = offset; i < offset + length; i++) {
        bool current = isnan(array[i]);
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
                int block_width = min(4, w - i);
                int block_height = min(4, h - j);
                for (int x = 0; x < block_width; x++) {
                    for (int y = 0; y < block_height; y++) {
                        float v = array[block_start + (y * w) + x];
                        if (!isnan(v)) {
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
                            if (isnan(v)) {
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
